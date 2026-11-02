#include "tmplgen_plugin_support.hpp"

#include "CodeComplete.h"
#include "Compiler.h"
#include "FeatureModule.h"
#include "LSPBinder.h"
#include "SemanticHighlighting.h"
#include "SourceCode.h"
#include "XRefs.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "Diagnostics.h"
#include "TUScheduler.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace clang::clangd {
namespace {

std::mutex GeneratorMutex;

// Full text of the most recent main-file (non-preamble) build per path. Used to
// feed the generator during preamble builds, which only see a truncated file.
std::mutex MainContentsMutex;
std::unordered_map<std::string, std::string> LastMainContents;

std::optional<std::string> decodeLineDirectivePath(llvm::StringRef Directive)
{
    size_t Quote = Directive.find('"');
    if (Quote == llvm::StringRef::npos) return std::nullopt;
    std::string Path;
    for (size_t I = Quote + 1U; I < Directive.size(); ++I) {
        char C = Directive[I];
        if (C == '"') return Path;
        if (C == '\\' && I + 1U < Directive.size()) {
            C = Directive[++I];
            if (C == 'n') C = '\n';
            else if (C == 't') C = '\t';
            else if (C == 'r') C = '\r';
        }
        Path.push_back(C);
    }
    return std::nullopt;
}

// `#line` paths of templates are written relative to the project root (see
// tmplgen --line-path-base). Resolve such a path by looking for it below each
// ancestor directory of the file that contains the directive.
std::string resolveLineDirectivePath(llvm::StringRef LinePath,
                                     llvm::StringRef ContainingFile)
{
    if (LinePath.empty() || llvm::sys::path::is_absolute(LinePath)) {
        return LinePath.str();
    }
    std::string Directory = llvm::sys::path::parent_path(ContainingFile).str();
    while (!Directory.empty()) {
        llvm::SmallString<256> Candidate(Directory);
        llvm::sys::path::append(Candidate, LinePath);
        if (llvm::sys::fs::exists(Candidate)) {
            llvm::sys::path::remove_dots(Candidate, true);
            return Candidate.str().str();
        }
        std::string Parent = llvm::sys::path::parent_path(Directory).str();
        if (Parent == Directory) break;
        Directory = std::move(Parent);
    }
    return LinePath.str();
}

bool isIdentifierChar(char C)
{
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
}

bool isTemplateSourcePath(llvm::StringRef Path)
{
    return Path.ends_with(".tmpl.h") || Path.ends_with(".tmpl.c");
}

struct SourceLineRecord {
    llvm::StringRef Text;
    size_t Start = 0U;
};

std::vector<SourceLineRecord> splitSourceLines(llvm::StringRef Code)
{
    std::vector<SourceLineRecord> Lines;
    size_t Start = 0U;
    while (Start < Code.size()) {
        size_t End = Code.find('\n', Start);
        if (End == llvm::StringRef::npos) End = Code.size();
        Lines.push_back(SourceLineRecord{Code.slice(Start, End), Start});
        if (End == Code.size()) break;
        Start = End + 1U;
    }
    if (Lines.empty()) Lines.push_back(SourceLineRecord{llvm::StringRef(), 0U});
    return Lines;
}

size_t lineNumberForOffset(llvm::ArrayRef<SourceLineRecord> Lines,
                           size_t Offset)
{
    size_t Result = 0U;
    for (size_t I = 0U; I < Lines.size() && Lines[I].Start <= Offset; ++I) {
        Result = I;
    }
    return Result;
}

llvm::StringRef firstIdentifier(llvm::StringRef Text)
{
    for (size_t I = 0U; I < Text.size(); ++I) {
        if (!isIdentifierChar(Text[I])) continue;
        size_t End = I + 1U;
        while (End < Text.size() && isIdentifierChar(Text[End])) ++End;
        return Text.slice(I, End);
    }
    return {};
}

std::optional<std::pair<llvm::StringRef, size_t>> lastIdentifier(
    llvm::StringRef Text)
{
    std::optional<std::pair<llvm::StringRef, size_t>> Result;
    for (size_t I = 0U; I < Text.size();) {
        if (!isIdentifierChar(Text[I])) {
            ++I;
            continue;
        }
        size_t Start = I++;
        while (I < Text.size() && isIdentifierChar(Text[I])) ++I;
        Result = std::pair<llvm::StringRef, size_t>{
            Text.slice(Start, I), Start};
    }
    return Result;
}

std::vector<llvm::StringRef> splitTopLevel(llvm::StringRef Text,
                                           char Delimiter)
{
    std::vector<llvm::StringRef> Parts;
    size_t Start = 0U;
    int Depth = 0;
    for (size_t I = 0U; I < Text.size(); ++I) {
        char Character = Text[I];
        if (Character == '(' || Character == '[' || Character == '{') {
            ++Depth;
        } else if (Character == ')' || Character == ']' ||
                   Character == '}') {
            if (Depth > 0) --Depth;
        } else if (Character == Delimiter && Depth == 0) {
            Parts.push_back(Text.slice(Start, I));
            Start = I + 1U;
        }
    }
    Parts.push_back(Text.drop_front(Start));
    return Parts;
}

struct TemplateAnnotationData {
    std::vector<std::string> Parameters;
    std::unordered_map<std::string, std::vector<std::string>> StructuralTypes;
};

struct ParsedAnnotationLine {
    bool IsDirective = false;
    bool IsTemplate = false;
    TemplateAnnotationData Data;
};

bool parseTemplateAnnotationLine(llvm::StringRef Line,
                                 ParsedAnnotationLine &Out)
{
    size_t Comment = Line.find("//");
    if (Comment == llvm::StringRef::npos) return false;
    llvm::StringRef Annotation = Line.drop_front(Comment + 2U).ltrim();

    llvm::StringRef Template = "@template";
    llvm::StringRef Where = "@where";
    bool IsTemplate = Annotation.starts_with(Template) &&
        (Annotation.size() == Template.size() ||
         !isIdentifierChar(Annotation[Template.size()]));
    bool IsWhere = Annotation.starts_with(Where) &&
        (Annotation.size() == Where.size() ||
         !isIdentifierChar(Annotation[Where.size()]));
    if (!IsTemplate && !IsWhere) return false;

    Out.IsDirective = true;
    Out.IsTemplate = IsTemplate;
    if (!IsTemplate) return true;

    llvm::StringRef Body = Annotation.drop_front(Template.size()).ltrim();
    for (llvm::StringRef Part : splitTopLevel(Body, ',')) {
        Part = Part.trim();
        if (Part.empty()) continue;
        size_t Structural = Part.find("<:");
        size_t Satisfaction = Part.find("|=");
        llvm::StringRef Left = Part;
        if (Structural != llvm::StringRef::npos) {
            Left = Part.take_front(Structural);
        } else if (Satisfaction != llvm::StringRef::npos) {
            Left = Part.take_front(Satisfaction);
        }
        llvm::StringRef Parameter = firstIdentifier(Left);
        if (Parameter.empty()) continue;
        std::string ParameterName = Parameter.str();
        if (std::find(Out.Data.Parameters.begin(), Out.Data.Parameters.end(),
                      ParameterName) == Out.Data.Parameters.end()) {
            Out.Data.Parameters.push_back(ParameterName);
        }
        if (Structural == llvm::StringRef::npos) continue;
        llvm::StringRef Requirements = Part.drop_front(Structural + 2U);
        std::vector<std::string> &Types =
            Out.Data.StructuralTypes[ParameterName];
        for (llvm::StringRef Requirement : splitTopLevel(Requirements, '+')) {
            Requirement = Requirement.trim();
            llvm::StringRef Type = lastIdentifier(Requirement)
                ? lastIdentifier(Requirement)->first : llvm::StringRef();
            if (Type.empty()) continue;
            std::string TypeName = Type.str();
            if (std::find(Types.begin(), Types.end(), TypeName) == Types.end()) {
                Types.push_back(std::move(TypeName));
            }
        }
    }
    return true;
}

bool isFallbackStart(llvm::StringRef Line)
{
    llvm::StringRef Text = Line.trim();
    if (!Text.consume_front("#")) return false;
    Text = Text.ltrim();
    if (!Text.consume_front("ifndef")) return false;
    if (!Text.empty() && isIdentifierChar(Text.front())) return false;
    Text = Text.ltrim();
    llvm::StringRef Guard = Text.take_while(isIdentifierChar);
    if (Guard != "TMPLGEN") return false;
    Text = Text.drop_front(Guard.size()).ltrim();
    return Text.empty();
}

bool isPreprocessorOpen(llvm::StringRef Line)
{
    llvm::StringRef Text = Line.trim();
    if (!Text.consume_front("#")) return false;
    Text = Text.ltrim();
    for (llvm::StringRef Keyword : {"if", "ifdef", "ifndef"}) {
        if (!Text.starts_with(Keyword)) continue;
        if (Text.size() != Keyword.size() &&
            isIdentifierChar(Text[Keyword.size()])) {
            continue;
        }
        return true;
    }
    return false;
}

bool isPreprocessorEndif(llvm::StringRef Line)
{
    llvm::StringRef Text = Line.trim();
    if (!Text.consume_front("#")) return false;
    Text = Text.ltrim();
    llvm::StringRef Keyword = "endif";
    if (!Text.starts_with(Keyword)) return false;
    return Text.size() == Keyword.size() ||
        !isIdentifierChar(Text[Keyword.size()]);
}

struct TemplateLineRange {
    size_t Start = 0U;
    size_t End = 0U;

    bool contains(size_t Line) const
    {
        return Line >= Start && Line <= End;
    }
};

struct TemplateDeclaration {
    size_t StartLine = 0U;
    size_t EndLine = 0U;
    size_t BodyStartLine = 0U;
    size_t BodyEndLine = 0U;
    bool IsFunction = false;
    // Offsets of the body braces in the source, or npos without a body.
    size_t BodyOpenOffset = llvm::StringRef::npos;
    size_t BodyCloseOffset = llvm::StringRef::npos;
    std::string Name;
    std::vector<std::string> Parameters;
    std::unordered_map<std::string, std::string> ParameterTypes;
    // Function parameters in declaration order as (name, type spelling).
    std::vector<std::pair<std::string, std::string>> OrderedParameters;
    std::unordered_map<std::string, std::vector<std::string>> StructuralTypes;
};

struct TemplateModel {
    std::vector<TemplateLineRange> FallbackRanges;
    std::vector<TemplateDeclaration> Declarations;

    bool inFallback(size_t Line) const
    {
        for (const TemplateLineRange &Range : FallbackRanges) {
            if (Range.contains(Line)) return true;
        }
        return false;
    }

    // Fallback definitions only exist so that the template parses as plain
    // C. Diagnostics inside them, or diagnostics in a template declaration that
    // talk about one of its template parameters (and are therefore about the
    // fallback stand-in rather than the real type), are noise.
    bool shouldSuppressDiagnostic(size_t Line,
                                  llvm::StringRef Message) const;

    const TemplateDeclaration *declarationAt(size_t Line) const
    {
        for (const TemplateDeclaration &Declaration : Declarations) {
            if (Line >= Declaration.StartLine &&
                Line <= Declaration.EndLine) {
                return &Declaration;
            }
        }
        return nullptr;
    }
};

bool isSkippableDeclarationLine(llvm::StringRef Line)
{
    llvm::StringRef Text = Line.trim();
    return Text.empty() || Text.starts_with("//") ||
        Text.starts_with("/*") || Text.starts_with("*") ||
        Text.starts_with("#");
}

size_t findHeaderEnd(llvm::StringRef Code, size_t Start)
{
    bool InLineComment = false;
    bool InBlockComment = false;
    bool InString = false;
    bool InCharacter = false;
    bool Escaped = false;
    for (size_t I = Start; I < Code.size(); ++I) {
        char Character = Code[I];
        if (InLineComment) {
            if (Character == '\n') InLineComment = false;
            continue;
        }
        if (InBlockComment) {
            if (Character == '*' && I + 1U < Code.size() &&
                Code[I + 1U] == '/') {
                InBlockComment = false;
                ++I;
            }
            continue;
        }
        if (InString || InCharacter) {
            if (Escaped) {
                Escaped = false;
            } else if (Character == '\\') {
                Escaped = true;
            } else if ((InString && Character == '"') ||
                       (InCharacter && Character == '\'')) {
                InString = false;
                InCharacter = false;
            }
            continue;
        }
        if (Character == '/' && I + 1U < Code.size()) {
            if (Code[I + 1U] == '/') {
                InLineComment = true;
                ++I;
                continue;
            }
            if (Code[I + 1U] == '*') {
                InBlockComment = true;
                ++I;
                continue;
            }
        }
        if (Character == '"') {
            InString = true;
            continue;
        }
        if (Character == '\'') {
            InCharacter = true;
            continue;
        }
        if (Character == ';' || Character == '{') return I;
    }
    return Code.size();
}

size_t matchingBrace(llvm::StringRef Code, size_t Open)
{
    bool InLineComment = false;
    bool InBlockComment = false;
    bool InString = false;
    bool InCharacter = false;
    bool Escaped = false;
    int Depth = 0;
    for (size_t I = Open; I < Code.size(); ++I) {
        char Character = Code[I];
        if (InLineComment) {
            if (Character == '\n') InLineComment = false;
            continue;
        }
        if (InBlockComment) {
            if (Character == '*' && I + 1U < Code.size() &&
                Code[I + 1U] == '/') {
                InBlockComment = false;
                ++I;
            }
            continue;
        }
        if (InString || InCharacter) {
            if (Escaped) {
                Escaped = false;
            } else if (Character == '\\') {
                Escaped = true;
            } else if ((InString && Character == '"') ||
                       (InCharacter && Character == '\'')) {
                InString = false;
                InCharacter = false;
            }
            continue;
        }
        if (Character == '/' && I + 1U < Code.size()) {
            if (Code[I + 1U] == '/') {
                InLineComment = true;
                ++I;
                continue;
            }
            if (Code[I + 1U] == '*') {
                InBlockComment = true;
                ++I;
                continue;
            }
        }
        if (Character == '"') {
            InString = true;
            continue;
        }
        if (Character == '\'') {
            InCharacter = true;
            continue;
        }
        if (Character == '{') {
            ++Depth;
        } else if (Character == '}') {
            --Depth;
            if (Depth == 0) return I;
        }
    }
    return Code.size();
}

size_t findSemicolon(llvm::StringRef Code, size_t Start)
{
    bool InLineComment = false;
    bool InBlockComment = false;
    bool InString = false;
    bool InCharacter = false;
    bool Escaped = false;
    for (size_t I = Start; I < Code.size(); ++I) {
        char Character = Code[I];
        if (InLineComment) {
            if (Character == '\n') InLineComment = false;
            continue;
        }
        if (InBlockComment) {
            if (Character == '*' && I + 1U < Code.size() &&
                Code[I + 1U] == '/') {
                InBlockComment = false;
                ++I;
            }
            continue;
        }
        if (InString || InCharacter) {
            if (Escaped) {
                Escaped = false;
            } else if (Character == '\\') {
                Escaped = true;
            } else if ((InString && Character == '"') ||
                       (InCharacter && Character == '\'')) {
                InString = false;
                InCharacter = false;
            }
            continue;
        }
        if (Character == '/' && I + 1U < Code.size()) {
            if (Code[I + 1U] == '/') {
                InLineComment = true;
                ++I;
                continue;
            }
            if (Code[I + 1U] == '*') {
                InBlockComment = true;
                ++I;
                continue;
            }
        }
        if (Character == '"') {
            InString = true;
            continue;
        }
        if (Character == '\'') {
            InCharacter = true;
            continue;
        }
        if (Character == ';') return I;
    }
    return Code.size();
}

bool isTypeDeclarationHeader(llvm::StringRef Header)
{
    Header = Header.trim();
    return Header.starts_with("typedef struct") ||
        Header.starts_with("typedef union") ||
        Header.starts_with("typedef enum") ||
        Header.starts_with("struct ") ||
        Header.starts_with("union ") ||
        Header.starts_with("enum ");
}

std::string declarationName(llvm::StringRef Header, bool IsFunction)
{
    if (IsFunction) {
        size_t Open = Header.find('(');
        if (Open == llvm::StringRef::npos) return {};
        llvm::StringRef Prefix = Header.take_front(Open).rtrim();
        auto Name = lastIdentifier(Prefix);
        return Name ? Name->first.str() : std::string();
    }
    size_t Close = Header.rfind('}');
    llvm::StringRef Search = Close == llvm::StringRef::npos
        ? Header : Header.drop_front(Close + 1U);
    llvm::StringRef Name = firstIdentifier(Search);
    if (!Name.empty()) return Name.str();
    for (llvm::StringRef Keyword : {"struct", "union", "enum"}) {
        size_t Found = Header.find(Keyword);
        if (Found == llvm::StringRef::npos) continue;
        llvm::StringRef Rest = Header.drop_front(Found + Keyword.size());
        Name = firstIdentifier(Rest);
        if (!Name.empty()) return Name.str();
    }
    return {};
}

void parseFunctionParameters(llvm::StringRef Header,
                             TemplateDeclaration &Declaration)
{
    size_t Open = Header.find('(');
    if (Open == llvm::StringRef::npos) return;
    int Depth = 0;
    size_t Close = llvm::StringRef::npos;
    for (size_t I = Open; I < Header.size(); ++I) {
        if (Header[I] == '(') {
            ++Depth;
        } else if (Header[I] == ')') {
            --Depth;
            if (Depth == 0) {
                Close = I;
                break;
            }
        }
    }
    if (Close == llvm::StringRef::npos) return;
    llvm::StringRef Parameters = Header.slice(Open + 1U, Close);
    for (llvm::StringRef Parameter : splitTopLevel(Parameters, ',')) {
        Parameter = Parameter.trim();
        if (Parameter.empty() || Parameter == "void" ||
            Parameter == "...") {
            continue;
        }
        size_t Pointer = Parameter.find("(*");
        if (Pointer != llvm::StringRef::npos) {
            size_t NameStart = Pointer + 2U;
            while (NameStart < Parameter.size() &&
                   !isIdentifierChar(Parameter[NameStart])) {
                ++NameStart;
            }
            size_t NameEnd = NameStart;
            while (NameEnd < Parameter.size() &&
                   isIdentifierChar(Parameter[NameEnd])) {
                ++NameEnd;
            }
            if (NameEnd == NameStart) continue;
            std::string Name = Parameter.slice(NameStart, NameEnd).str();
            std::string Type = Parameter.take_front(Pointer).trim().str();
            Declaration.ParameterTypes[Name] = Type;
            // Function pointer types are not modelled; leave unchecked.
            Declaration.OrderedParameters.emplace_back(Name, std::string());
            continue;
        }
        // Array parameters decay to pointers: `T values[N]` is `T *`.
        bool IsArray = false;
        while (Parameter.ends_with("]")) {
            size_t Open = Parameter.rfind('[');
            if (Open == llvm::StringRef::npos) break;
            Parameter = Parameter.take_front(Open).rtrim();
            IsArray = true;
        }
        auto Name = lastIdentifier(Parameter);
        if (!Name) continue;
        llvm::StringRef TypeText = Parameter.take_front(Name->second).trim();
        if (TypeText.empty()) continue;
        std::string Type = TypeText.str();
        if (IsArray) Type += " *";
        Declaration.ParameterTypes[Name->first.str()] = Type;
        Declaration.OrderedParameters.emplace_back(Name->first.str(), Type);
    }
}

TemplateModel parseTemplateModel(llvm::StringRef Code)
{
    TemplateModel Model;
    std::vector<SourceLineRecord> Lines = splitSourceLines(Code);

    size_t FallbackStart = 0U;
    size_t FallbackDepth = 0U;
    for (size_t I = 0U; I < Lines.size(); ++I) {
        if (FallbackDepth != 0U) {
            if (isPreprocessorOpen(Lines[I].Text)) {
                ++FallbackDepth;
            } else if (isPreprocessorEndif(Lines[I].Text)) {
                --FallbackDepth;
                if (FallbackDepth == 0U) {
                    Model.FallbackRanges.push_back(
                        TemplateLineRange{FallbackStart, I});
                }
            }
        } else if (isFallbackStart(Lines[I].Text)) {
            FallbackStart = I;
            FallbackDepth = 1U;
        }
    }

    for (size_t I = 0U; I < Lines.size();) {
        ParsedAnnotationLine First;
        if (!parseTemplateAnnotationLine(Lines[I].Text, First)) {
            ++I;
            continue;
        }

        TemplateAnnotationData AnnotationData;
        if (First.IsTemplate) {
            AnnotationData = std::move(First.Data);
        }
        size_t J = I + 1U;
        while (J < Lines.size()) {
            ParsedAnnotationLine Next;
            if (!parseTemplateAnnotationLine(Lines[J].Text, Next)) break;
            if (Next.IsTemplate) {
                for (const std::string &Parameter : Next.Data.Parameters) {
                    if (std::find(AnnotationData.Parameters.begin(),
                                  AnnotationData.Parameters.end(),
                                  Parameter) ==
                        AnnotationData.Parameters.end()) {
                        AnnotationData.Parameters.push_back(Parameter);
                    }
                }
                for (const auto &Entry : Next.Data.StructuralTypes) {
                    std::vector<std::string> &Types =
                        AnnotationData.StructuralTypes[Entry.first];
                    for (const std::string &Type : Entry.second) {
                        if (std::find(Types.begin(), Types.end(), Type) ==
                            Types.end()) {
                            Types.push_back(Type);
                        }
                    }
                }
            }
            ++J;
        }

        size_t K = J;
        while (K < Lines.size() &&
               isSkippableDeclarationLine(Lines[K].Text)) {
            ++K;
        }
        if (K >= Lines.size()) {
            I = J;
            continue;
        }

        size_t StartOffset = Lines[K].Start;
        size_t HeaderEnd = findHeaderEnd(Code, StartOffset);
        if (HeaderEnd >= Code.size()) {
            I = J;
            continue;
        }
        llvm::StringRef Header = Code.slice(StartOffset, HeaderEnd);
        bool IsFunction = !isTypeDeclarationHeader(Header) &&
            Header.contains('(');

        size_t BodyOpen = llvm::StringRef::npos;
        size_t BodyClose = llvm::StringRef::npos;
        size_t EndOffset = HeaderEnd;
        if (Code[HeaderEnd] == '{') {
            BodyOpen = HeaderEnd;
            BodyClose = matchingBrace(Code, BodyOpen);
            if (IsFunction) {
                EndOffset = BodyClose;
            } else {
                size_t Semicolon = findSemicolon(Code, BodyClose);
                EndOffset = Semicolon < Code.size() ? Semicolon : BodyClose;
            }
        }

        TemplateDeclaration Declaration;
        Declaration.StartLine = K;
        Declaration.EndLine = lineNumberForOffset(Lines, EndOffset);
        Declaration.BodyStartLine = BodyOpen == llvm::StringRef::npos
            ? K : lineNumberForOffset(Lines, BodyOpen);
        Declaration.BodyEndLine = BodyClose == llvm::StringRef::npos
            ? Declaration.EndLine
            : lineNumberForOffset(Lines, BodyClose);
        Declaration.IsFunction = IsFunction;
        Declaration.BodyOpenOffset = BodyOpen;
        Declaration.BodyCloseOffset = BodyClose;
        Declaration.Name = declarationName(Header, IsFunction);
        Declaration.Parameters = std::move(AnnotationData.Parameters);
        Declaration.StructuralTypes =
            std::move(AnnotationData.StructuralTypes);
        if (IsFunction) parseFunctionParameters(Header, Declaration);
        size_t DeclarationEndLine = Declaration.EndLine;
        Model.Declarations.push_back(std::move(Declaration));

        I = DeclarationEndLine + 1U;
    }
    return Model;
}

std::optional<size_t> identifierPosition(llvm::StringRef Text,
                                         llvm::StringRef Name)
{
    size_t Position = 0U;
    while ((Position = Text.find(Name, Position)) != llvm::StringRef::npos) {
        size_t End = Position + Name.size();
        if ((Position == 0U || !isIdentifierChar(Text[Position - 1U])) &&
            (End == Text.size() || !isIdentifierChar(Text[End]))) {
            return Position;
        }
        Position = End;
    }
    return std::nullopt;
}

bool messageQuotesIdentifier(llvm::StringRef Message, llvm::StringRef Name)
{
    size_t Open = 0U;
    while ((Open = Message.find('\'', Open)) != llvm::StringRef::npos) {
        size_t Close = Message.find('\'', Open + 1U);
        if (Close == llvm::StringRef::npos) return false;
        if (identifierPosition(Message.slice(Open + 1U, Close), Name)) {
            return true;
        }
        Open = Close + 1U;
    }
    return false;
}

bool TemplateModel::shouldSuppressDiagnostic(size_t Line,
                                             llvm::StringRef Message) const
{
    if (Message.starts_with("tmplgen:")) return false;
    if (inFallback(Line)) return true;
    const TemplateDeclaration *Declaration = declarationAt(Line);
    if (Declaration == nullptr) return false;
    for (const std::string &Parameter : Declaration->Parameters) {
        if (messageQuotesIdentifier(Message, Parameter)) return true;
    }
    return false;
}

int braceDepthAt(llvm::StringRef Text, size_t Position)
{
    int Depth = 0;
    for (size_t I = 0U; I < Position; ++I) {
        if (Text[I] == '{') ++Depth;
        else if (Text[I] == '}' && Depth > 0) --Depth;
    }
    return Depth;
}

bool namesTypeDeclaration(llvm::StringRef Code, llvm::StringRef Name,
                          size_t &Column)
{
    std::optional<size_t> Position = identifierPosition(Code, Name);
    if (!Position || braceDepthAt(Code, *Position) != 0) return false;
    size_t End = *Position + Name.size();
    bool HasSemicolon = Code.find(';', End) != llvm::StringRef::npos;
    bool HasClosingBrace = Code.rfind('}', *Position) != llvm::StringRef::npos;
    bool HasTagKeyword = false;
    for (llvm::StringRef Keyword : {"struct", "union"}) {
        size_t Found = Code.take_front(*Position).rfind(Keyword);
        if (Found == llvm::StringRef::npos) continue;
        size_t KeywordEnd = Found + Keyword.size();
        if (KeywordEnd <= *Position &&
            Code.slice(KeywordEnd, *Position).trim().empty()) {
            HasTagKeyword = true;
            break;
        }
    }
    if (!HasSemicolon && !HasClosingBrace && !HasTagKeyword) return false;
    Column = *Position;
    return true;
}

std::optional<Location> fallbackTemplateTypeLocation(
    const LocatedSymbol &Symbol, const Location &Definition,
    llvm::StringRef MainPath)
{
    llvm::StringRef Path = Definition.uri.file();
    if (!Path.ends_with(".tmpl.h") && !Path.ends_with(".tmpl.c")) {
        return std::nullopt;
    }
    auto Buffer = llvm::MemoryBuffer::getFile(Path);
    if (!Buffer) return std::nullopt;
    llvm::SmallVector<llvm::StringRef, 256> Lines;
    (*Buffer)->getBuffer().split(Lines, '\n');
    size_t TargetLine = Definition.range.start.line;
    if (TargetLine >= Lines.size()) return std::nullopt;

    llvm::StringRef MacroLine = Lines[TargetLine].trim();
    if (!MacroLine.consume_front("#define")) return std::nullopt;
    MacroLine = MacroLine.ltrim();
    llvm::StringRef MacroName = MacroLine.take_while(isIdentifierChar);
    if (MacroName != Symbol.Name) return std::nullopt;

    bool PendingTemplate = false;
    bool InTypeDeclaration = false;
    int Depth = 0;
    for (size_t I = TargetLine + 1U; I < Lines.size(); ++I) {
        llvm::StringRef Trimmed = Lines[I].trim();
        if (Trimmed.starts_with("// @template")) {
            PendingTemplate = true;
            InTypeDeclaration = false;
            Depth = 0;
            continue;
        }
        if (!PendingTemplate || Trimmed.empty() ||
            Trimmed.starts_with("//") || Trimmed.starts_with("#")) {
            continue;
        }
        llvm::StringRef Code = Trimmed;
        size_t Comment = Code.find("//");
        if (Comment != llvm::StringRef::npos) Code = Code.take_front(Comment);
        if (!InTypeDeclaration) {
            if (!Code.starts_with("typedef") && !Code.starts_with("struct") &&
                !Code.starts_with("union")) {
                PendingTemplate = false;
                continue;
            }
            InTypeDeclaration = true;
            Depth = 0;
        }
        size_t Column = 0U;
        if (namesTypeDeclaration(Code, Symbol.Name, Column)) {
            return Location{
                URIForFile::canonicalize(Path, MainPath),
                Range{{static_cast<int>(I), static_cast<int>(Column)},
                      {static_cast<int>(I),
                       static_cast<int>(Column + Symbol.Name.size())}}};
        }
        for (char C : Code) {
            if (C == '{') ++Depth;
            else if (C == '}' && Depth > 0) --Depth;
        }
        if (Depth == 0 && Code.contains(';')) {
            PendingTemplate = false;
            InTypeDeclaration = false;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<llvm::StringRef, size_t>> constraintWordAt(
    llvm::StringRef Line, size_t Cursor)
{
    size_t Comment = Line.find("//");
    if (Comment == llvm::StringRef::npos) return std::nullopt;
    llvm::StringRef Annotation = Line.drop_front(Comment + 2U);
    if (!Annotation.contains("@template") && !Annotation.contains("@where")) {
        return std::nullopt;
    }

    struct Operator {
        size_t Start;
        size_t End;
    };
    llvm::SmallVector<Operator, 4> Operators;
    for (size_t I = Comment + 2U; I + 1U < Line.size(); ++I) {
        llvm::StringRef Candidate = Line.substr(I, 2U);
        if (Candidate == "<:" || Candidate == "|=") {
            Operators.push_back({I, I + 2U});
            ++I;
        }
    }

    const Operator *Current = nullptr;
    for (const Operator &Candidate : Operators) {
        if (Candidate.Start <= Cursor) Current = &Candidate;
    }
    if (Current == nullptr) return std::nullopt;

    size_t ExpressionEnd = Line.size();
    int Depth = 0;
    for (size_t I = Current->End; I < Line.size(); ++I) {
        char Character = Line[I];
        if (Character == '(' || Character == '[' || Character == '{') {
            ++Depth;
        } else if (Character == ')' || Character == ']' || Character == '}') {
            if (Depth > 0) --Depth;
        } else if (Character == ',' && Depth == 0) {
            ExpressionEnd = I;
            break;
        }
    }
    if (Cursor > ExpressionEnd) return std::nullopt;

    size_t TokenStart = Cursor;
    while (TokenStart > 0U && isIdentifierChar(Line[TokenStart - 1U])) {
        --TokenStart;
    }
    size_t TokenEnd = Cursor;
    while (TokenEnd < Line.size() && isIdentifierChar(Line[TokenEnd])) {
        ++TokenEnd;
    }
    bool OnOperator = Cursor >= Current->Start && Cursor <= Current->End;
    if (!OnOperator && TokenStart < TokenEnd &&
        TokenStart >= Current->End && TokenStart < ExpressionEnd) {
        return std::pair<llvm::StringRef, size_t>{
            Line.slice(TokenStart, TokenEnd), TokenStart};
    }

    size_t Search = std::max(Cursor, Current->End);
    while (Search < ExpressionEnd && !isIdentifierChar(Line[Search])) ++Search;
    if (Search == ExpressionEnd) return std::nullopt;
    size_t Start = Search;
    while (Start < ExpressionEnd && isIdentifierChar(Line[Start])) ++Start;
    return std::pair<llvm::StringRef, size_t>{
        Line.slice(Search, Start), Search};
}

std::optional<Location> sourceLocationToDefinition(
    const SourceManager &SourceMgr, const LangOptions &LangOpts,
    SourceLocation Loc, llvm::StringRef MainPath)
{
    if (Loc.isInvalid()) return std::nullopt;
    Loc = SourceMgr.getSpellingLoc(Loc);
    if (Loc.isInvalid()) return std::nullopt;
    FileID FID = SourceMgr.getFileID(Loc);
    if (FID.isInvalid()) return std::nullopt;
    auto File = SourceMgr.getFileEntryRefForID(FID);
    if (!File) return std::nullopt;
    std::optional<std::string> Path =
        getCanonicalPath(*File, SourceMgr.getFileManager());
    if (!Path) return std::nullopt;
    unsigned Length = Lexer::MeasureTokenLength(Loc, SourceMgr, LangOpts);
    if (Length == 0U) return std::nullopt;
    return Location{
        URIForFile::canonicalize(*Path, MainPath),
        halfOpenToRange(
            SourceMgr,
            CharSourceRange::getCharRange(
                Loc, Loc.getLocWithOffset(Length)))};
}

std::optional<Location> macroDefinitionLocation(
    ParsedAST &AST, llvm::StringRef Name, SourceLocation UseLoc,
    llvm::StringRef MainPath)
{
    Preprocessor &PP = AST.getPreprocessor();
    IdentifierInfo *Identifier = PP.getIdentifierInfo(Name);
    if (Identifier == nullptr || !Identifier->hadMacroDefinition()) {
        return std::nullopt;
    }

    MacroDefinition Definition;
    if (UseLoc.isValid()) {
        Definition = PP.getMacroDefinitionAtLoc(Identifier, UseLoc);
    }
    const MacroInfo *Info = Definition.getMacroInfo();
    if (Info == nullptr) {
        Definition = PP.getMacroDefinition(Identifier);
        Info = Definition.getMacroInfo();
    }
    if (Info == nullptr) return std::nullopt;
    return sourceLocationToDefinition(
        AST.getSourceManager(), AST.getLangOpts(),
        Info->getDefinitionLoc(), MainPath);
}

const NamedDecl *findTypeDeclaration(const DeclContext &Context,
                                     llvm::StringRef Name)
{
    const NamedDecl *Fallback = nullptr;
    for (const Decl *Declaration : Context.decls()) {
        const auto *Named = dyn_cast<NamedDecl>(Declaration);
        if (Named == nullptr || Named->getIdentifier() == nullptr ||
            Named->getName() != Name ||
            (!isa<TagDecl>(Named) && !isa<TypedefNameDecl>(Named))) {
            continue;
        }
        if (const auto *Tag = dyn_cast<TagDecl>(Named);
            Tag != nullptr && Tag->isThisDeclarationADefinition()) {
            return Named;
        }
        if (Fallback == nullptr) Fallback = Named;
    }
    for (const Decl *Declaration : Context.decls()) {
        const auto *Inner = dyn_cast<DeclContext>(Declaration);
        if (Inner == nullptr) continue;
        if (const NamedDecl *Found = findTypeDeclaration(*Inner, Name)) {
            return Found;
        }
    }
    return Fallback;
}

struct MemberAccess {
    std::string Base;
    size_t WordStart = 0U;
};

std::optional<MemberAccess> memberAccessBefore(llvm::StringRef Code,
                                               size_t Cursor)
{
    size_t NameStart = Cursor;
    while (NameStart > 0U && isIdentifierChar(Code[NameStart - 1U])) {
        --NameStart;
    }
    size_t OperatorEnd = NameStart;
    while (OperatorEnd > 0U &&
           std::isspace(static_cast<unsigned char>(Code[OperatorEnd - 1U]))) {
        --OperatorEnd;
    }
    size_t BaseEnd = 0U;
    if (OperatorEnd >= 2U && Code[OperatorEnd - 1U] == '>' &&
        Code[OperatorEnd - 2U] == '-') {
        BaseEnd = OperatorEnd - 2U;
    } else if (OperatorEnd >= 1U && Code[OperatorEnd - 1U] == '.') {
        BaseEnd = OperatorEnd - 1U;
    } else {
        return std::nullopt;
    }
    size_t BaseStart = BaseEnd;
    while (BaseStart > 0U && isIdentifierChar(Code[BaseStart - 1U])) {
        --BaseStart;
    }
    if (BaseStart == BaseEnd) return std::nullopt;
    return MemberAccess{Code.slice(BaseStart, BaseEnd).str(), NameStart};
}

const RecordDecl *findRecordDeclaration(const ASTContext &Context,
                                        llvm::StringRef Name)
{
    const NamedDecl *Named = findTypeDeclaration(
        *Context.getTranslationUnitDecl(), Name);
    if (Named == nullptr) return nullptr;
    if (const auto *Record = dyn_cast<RecordDecl>(Named)) return Record;
    if (const auto *Typedef = dyn_cast<TypedefNameDecl>(Named)) {
        if (const auto *Record = Typedef->getUnderlyingType()
                                     .getCanonicalType()
                                     ->getAs<RecordType>()) {
            return Record->getDecl();
        }
    }
    return nullptr;
}

bool isBuiltinTypeName(llvm::StringRef Name);

// ---------------------------------------------------------------------------
// Type spellings in template bodies.
//
// Inside a template the fallback definitions collapse every template parameter
// (and every generic type such as `Option(T)`) onto one concrete stand-in, so
// clang's own types cannot tell `Option(T)` from `Option(U)`. The helpers below
// work on the *spelled* types instead, the same view tmplgen itself has.
// ---------------------------------------------------------------------------

// Replaces comments, string and character literals with spaces so that
// offsets are preserved but their contents are never mistaken for code.
std::string maskCommentsAndLiterals(llvm::StringRef Code)
{
    std::string Masked = Code.str();
    enum class State { Code, Line, Block, String, Character } Current =
        State::Code;
    bool Escaped = false;
    for (size_t I = 0U; I < Masked.size(); ++I) {
        char Character = Code[I];
        switch (Current) {
        case State::Code:
            if (Character == '/' && I + 1U < Code.size() &&
                Code[I + 1U] == '/') {
                Current = State::Line;
                Masked[I] = ' ';
            } else if (Character == '/' && I + 1U < Code.size() &&
                       Code[I + 1U] == '*') {
                Current = State::Block;
                Masked[I] = ' ';
                Masked[++I] = ' ';
            } else if (Character == '"') {
                Current = State::String;
            } else if (Character == '\'') {
                Current = State::Character;
            }
            break;
        case State::Line:
            if (Character == '\n') Current = State::Code;
            else Masked[I] = ' ';
            break;
        case State::Block:
            if (Character == '*' && I + 1U < Code.size() &&
                Code[I + 1U] == '/') {
                Masked[I] = ' ';
                Masked[++I] = ' ';
                Current = State::Code;
            } else if (Character != '\n') {
                Masked[I] = ' ';
            }
            break;
        case State::String:
        case State::Character:
            if (Escaped) {
                Escaped = false;
                Masked[I] = ' ';
            } else if (Character == '\\') {
                Escaped = true;
                Masked[I] = ' ';
            } else if ((Current == State::String && Character == '"') ||
                       (Current == State::Character && Character == '\'')) {
                Current = State::Code;
            } else if (Character != '\n') {
                Masked[I] = ' ';
            }
            break;
        }
    }
    return Masked;
}

bool isIgnoredTypeQualifier(llvm::StringRef Word)
{
    for (llvm::StringRef Qualifier : {
             "const", "volatile", "restrict", "__restrict", "register",
             "static", "inline", "extern", "auto", "_Atomic"}) {
        if (Word == Qualifier) return true;
    }
    return false;
}

using TypeTokens = std::vector<std::string>;

TypeTokens typeTokens(llvm::StringRef Type)
{
    TypeTokens Tokens;
    for (size_t I = 0U; I < Type.size();) {
        char Character = Type[I];
        if (isIdentifierChar(Character)) {
            size_t Start = I;
            while (I < Type.size() && isIdentifierChar(Type[I])) ++I;
            llvm::StringRef Word = Type.slice(Start, I);
            if (!isIgnoredTypeQualifier(Word)) Tokens.push_back(Word.str());
            continue;
        }
        if (Character == '*' || Character == '(' || Character == ')' ||
            Character == ',' || Character == '[' || Character == ']') {
            Tokens.push_back(std::string(1U, Character));
        }
        ++I;
    }
    return Tokens;
}

bool isWordToken(const std::string &Token)
{
    return !Token.empty() && isIdentifierChar(Token[0]);
}

std::string spellTokens(llvm::ArrayRef<std::string> Tokens)
{
    std::string Result;
    for (size_t I = 0U; I < Tokens.size(); ++I) {
        const std::string &Token = Tokens[I];
        if (I != 0U) {
            const std::string &Previous = Tokens[I - 1U];
            if ((isWordToken(Previous) && isWordToken(Token)) ||
                (Token == "*" && Previous != "*" && Previous != "(")) {
                Result += ' ';
            }
        }
        Result += Token;
        if (Token == ",") Result += ' ';
    }
    return Result;
}

bool containsWord(llvm::ArrayRef<std::string> Tokens,
                  llvm::ArrayRef<std::string> Words)
{
    for (const std::string &Token : Tokens) {
        if (llvm::is_contained(Words, Token)) return true;
    }
    return false;
}

// A spelling is "generic" when it mentions a template parameter of the
// enclosing template or a generic type constructor such as `Option(T)`. Those
// are exactly the spellings that the fallback definitions cannot check.
bool isGenericSpelling(llvm::ArrayRef<std::string> Tokens,
                       llvm::ArrayRef<std::string> Parameters)
{
    if (containsWord(Tokens, Parameters)) return true;
    for (size_t I = 0U; I + 1U < Tokens.size(); ++I) {
        if (isWordToken(Tokens[I]) && Tokens[I + 1U] == "(") return true;
    }
    return false;
}

bool balancedTokens(llvm::ArrayRef<std::string> Tokens)
{
    int Depth = 0;
    for (const std::string &Token : Tokens) {
        if (Token == "(" || Token == "[") ++Depth;
        else if (Token == ")" || Token == "]") {
            if (--Depth < 0) return false;
        } else if (Token == "," && Depth == 0) {
            return false;
        }
    }
    return Depth == 0;
}

using TypeBindings = std::unordered_map<std::string, TypeTokens>;

// Matches the spelled parameter type `Pattern` (whose words in `Variables` are
// unification variables) against the spelled argument type `Target`.
bool unifyTypeTokens(llvm::ArrayRef<std::string> Pattern, size_t PatternIndex,
                     llvm::ArrayRef<std::string> Target, size_t TargetIndex,
                     llvm::ArrayRef<std::string> Variables,
                     TypeBindings &Bindings)
{
    if (PatternIndex == Pattern.size()) return TargetIndex == Target.size();
    const std::string &Token = Pattern[PatternIndex];
    if (!llvm::is_contained(Variables, Token)) {
        if (TargetIndex >= Target.size() || Target[TargetIndex] != Token) {
            return false;
        }
        return unifyTypeTokens(Pattern, PatternIndex + 1U, Target,
                               TargetIndex + 1U, Variables, Bindings);
    }

    auto Bound = Bindings.find(Token);
    if (Bound != Bindings.end()) {
        const TypeTokens &Value = Bound->second;
        if (Target.size() - TargetIndex < Value.size()) return false;
        for (size_t I = 0U; I < Value.size(); ++I) {
            if (Target[TargetIndex + I] != Value[I]) return false;
        }
        return unifyTypeTokens(Pattern, PatternIndex + 1U, Target,
                               TargetIndex + Value.size(), Variables,
                               Bindings);
    }

    for (size_t Length = 1U; TargetIndex + Length <= Target.size();
         ++Length) {
        llvm::ArrayRef<std::string> Candidate =
            Target.slice(TargetIndex, Length);
        if (!isWordToken(Candidate.front()) || !balancedTokens(Candidate)) {
            continue;
        }
        Bindings[Token] = TypeTokens(Candidate.begin(), Candidate.end());
        if (unifyTypeTokens(Pattern, PatternIndex + 1U, Target,
                            TargetIndex + Length, Variables, Bindings)) {
            return true;
        }
        Bindings.erase(Token);
    }
    return false;
}

bool isTypeStartWord(llvm::StringRef Word,
                     llvm::ArrayRef<std::string> Parameters)
{
    if (isBuiltinTypeName(Word) || isIgnoredTypeQualifier(Word)) return true;
    for (llvm::StringRef Keyword : {"struct", "union", "enum"}) {
        if (Word == Keyword) return true;
    }
    return llvm::is_contained(Parameters, Word.str());
}

bool isStatementKeyword(llvm::StringRef Word)
{
    for (llvm::StringRef Keyword : {
             "return", "goto", "case", "sizeof", "else", "do", "if",
             "while", "switch", "break", "continue", "default"}) {
        if (Word == Keyword) return true;
    }
    return false;
}

// Finds the declared type of the local variable `Name` in the masked body
// text. The last matching declaration wins.
std::optional<std::string> localDeclarationType(llvm::StringRef Body,
                                                llvm::StringRef Name)
{
    std::optional<std::string> Result;
    size_t Start = 0U;
    for (size_t I = 0U; I <= Body.size(); ++I) {
        if (I != Body.size() && Body[I] != ';' && Body[I] != '{' &&
            Body[I] != '}') {
            continue;
        }
        llvm::StringRef Statement = Body.slice(Start, I).trim();
        Start = I + 1U;
        if (Statement.consume_front("for")) {
            Statement = Statement.ltrim();
            if (!Statement.consume_front("(")) continue;
            Statement = Statement.ltrim();
        }
        std::optional<size_t> Position = identifierPosition(Statement, Name);
        if (!Position || *Position == 0U) continue;
        llvm::StringRef Before = Statement.take_front(*Position).rtrim();
        llvm::StringRef After =
            Statement.drop_front(*Position + Name.size()).ltrim();
        if (!After.empty() && !After.starts_with("=") &&
            !After.starts_with("[")) {
            continue;
        }
        if (After.starts_with("==")) continue;
        if (Before.empty() || Before.contains('=') || Before.contains(';')) {
            continue;
        }
        char Last = Before.back();
        if (!isIdentifierChar(Last) && Last != '*' && Last != ')') continue;
        llvm::StringRef First = firstIdentifier(Before);
        if (First.empty() || !Before.starts_with(First) ||
            isStatementKeyword(First)) {
            continue;
        }
        // Multiple declarators (`int a, b`) are not modelled.
        TypeTokens Tokens = typeTokens(Before);
        if (!balancedTokens(Tokens)) continue;
        // Reject expression statements such as `a * b` where the leading word
        // cannot start a type: it must be a known type word, a generic type
        // constructor, or a plain typedef name followed only by `*`.
        bool GenericConstructor = Tokens.size() > 1U && Tokens[1] == "(";
        bool PlainTypedef = true;
        for (size_t T = 1U; T < Tokens.size(); ++T) {
            if (Tokens[T] != "*" && !isWordToken(Tokens[T])) {
                PlainTypedef = false;
            }
        }
        if (!GenericConstructor && !PlainTypedef) continue;
        std::string Type = Before.str();
        if (After.starts_with("[")) Type += " *";
        Result = std::move(Type);
    }
    return Result;
}

// The spelled type of `Name` as seen at `Offset` inside the declaration body.
std::optional<std::string> variableTypeInDeclaration(
    const TemplateDeclaration &Declaration, llvm::StringRef MaskedCode,
    llvm::StringRef Name, size_t Offset)
{
    if (Declaration.BodyOpenOffset != llvm::StringRef::npos &&
        Declaration.BodyOpenOffset < Offset) {
        size_t End = std::min(Offset, Declaration.BodyCloseOffset);
        if (std::optional<std::string> Local = localDeclarationType(
                MaskedCode.slice(Declaration.BodyOpenOffset + 1U, End),
                Name)) {
            return Local;
        }
    }
    auto Parameter = Declaration.ParameterTypes.find(Name.str());
    if (Parameter != Declaration.ParameterTypes.end() &&
        !Parameter->second.empty()) {
        return Parameter->second;
    }
    return std::nullopt;
}

struct StructuralField {
    std::string Name;
    std::string Type;
    std::string Canonical;
    std::string Schema;
};

std::vector<StructuralField> recordFields(const RecordDecl &Record,
                                          llvm::StringRef Schema)
{
    std::vector<StructuralField> Fields;
    for (const FieldDecl *Field : Record.fields()) {
        std::string FieldName = Field->getNameAsString();
        if (FieldName.empty()) continue;
        Fields.push_back(StructuralField{
            std::move(FieldName), Field->getType().getAsString(),
            Field->getType().getCanonicalType().getAsString(),
            Schema.str()});
    }
    return Fields;
}

std::vector<StructuralField> structuralFields(
    const ASTContext &Context, llvm::ArrayRef<std::string> Schemas,
    std::vector<std::string> *Resolved = nullptr)
{
    std::vector<StructuralField> Fields;
    for (const std::string &SchemaName : Schemas) {
        const RecordDecl *Record = findRecordDeclaration(Context, SchemaName);
        if (Record == nullptr) continue;
        if (Resolved != nullptr) Resolved->push_back(SchemaName);
        for (StructuralField &Field : recordFields(*Record, SchemaName)) {
            bool Seen = llvm::any_of(Fields, [&](const StructuralField &E) {
                return E.Name == Field.Name;
            });
            if (!Seen) Fields.push_back(std::move(Field));
        }
    }
    return Fields;
}

std::string joinNames(llvm::ArrayRef<std::string> Names,
                      llvm::StringRef Separator)
{
    std::string Result;
    for (size_t I = 0U; I < Names.size(); ++I) {
        if (I != 0U) Result += Separator.str();
        Result += Names[I];
    }
    return Result;
}

// If `Type` is `S` (for `.`) or `S *` (for `->`) where `S` is a structurally
// constrained template parameter, returns `S`.
std::optional<std::string> structuralParameterForAccess(
    const TemplateDeclaration &Declaration, llvm::StringRef Type, bool Arrow)
{
    TypeTokens Tokens = typeTokens(Type);
    if (Tokens.empty()) return std::nullopt;
    size_t Pointers = 0U;
    while (!Tokens.empty() && Tokens.back() == "*") {
        Tokens.pop_back();
        ++Pointers;
    }
    if (Tokens.size() != 1U || Pointers != (Arrow ? 1U : 0U)) {
        return std::nullopt;
    }
    auto Requirements = Declaration.StructuralTypes.find(Tokens.front());
    if (Requirements == Declaration.StructuralTypes.end() ||
        Requirements->second.empty()) {
        return std::nullopt;
    }
    return Tokens.front();
}

struct StructuralAccessContext {
    const TemplateDeclaration *Declaration = nullptr;
    std::string Parameter;
    size_t WordStart = 0U;
};

// Text-only part of structural completion: is the cursor right after
// `variable->` / `variable.` for a structurally constrained variable?
std::optional<StructuralAccessContext> structuralAccessAt(
    const TemplateModel &Model, llvm::StringRef Code, Position Position)
{
    llvm::Expected<size_t> Offset = positionToOffset(Code, Position, false);
    if (!Offset) {
        llvm::consumeError(Offset.takeError());
        return std::nullopt;
    }
    std::optional<MemberAccess> Access = memberAccessBefore(Code, *Offset);
    if (!Access) return std::nullopt;
    const TemplateDeclaration *Declaration =
        Model.declarationAt(Position.line);
    if (Declaration == nullptr || Declaration->StructuralTypes.empty() ||
        Declaration->BodyOpenOffset == llvm::StringRef::npos ||
        *Offset <= Declaration->BodyOpenOffset ||
        *Offset > Declaration->BodyCloseOffset) {
        return std::nullopt;
    }
    std::string Masked = maskCommentsAndLiterals(Code);
    std::optional<std::string> Type = variableTypeInDeclaration(
        *Declaration, Masked, Access->Base, *Offset);
    if (!Type) return std::nullopt;
    size_t OperatorEnd = Access->WordStart;
    while (OperatorEnd > 0U &&
           std::isspace(static_cast<unsigned char>(Code[OperatorEnd - 1U]))) {
        --OperatorEnd;
    }
    bool Arrow = Code[OperatorEnd - 1U] == '>';
    std::optional<std::string> Parameter =
        structuralParameterForAccess(*Declaration, *Type, Arrow);
    if (!Parameter) return std::nullopt;
    return StructuralAccessContext{Declaration, std::move(*Parameter),
                                   Access->WordStart};
}

// ---------------------------------------------------------------------------
// Template body checks.
// ---------------------------------------------------------------------------

struct TemplateIssue {
    size_t Offset = 0U;
    std::string Message;
    // Drop this issue when clang already reported a diagnostic at Offset (the
    // mismatch is visible even through the fallback definitions).
    bool DeferToClang = false;
};

void checkStructuralFieldAccesses(const ASTContext &Context,
                                  const TemplateDeclaration &Declaration,
                                  llvm::StringRef Masked,
                                  std::vector<TemplateIssue> &Issues)
{
    if (Declaration.StructuralTypes.empty()) return;
    size_t Begin = Declaration.BodyOpenOffset + 1U;
    size_t End = Declaration.BodyCloseOffset;
    for (size_t I = Begin; I < End;) {
        if (!isIdentifierChar(Masked[I]) ||
            std::isdigit(static_cast<unsigned char>(Masked[I]))) {
            ++I;
            continue;
        }
        size_t NameStart = I;
        while (I < End && isIdentifierChar(Masked[I])) ++I;
        llvm::StringRef Name = Masked.slice(NameStart, I);

        // Only the head of an access chain names a variable.
        size_t Previous = NameStart;
        while (Previous > Begin &&
               std::isspace(static_cast<unsigned char>(Masked[Previous - 1U]))) {
            --Previous;
        }
        if (Previous > Begin &&
            (Masked[Previous - 1U] == '.' ||
             (Masked[Previous - 1U] == '>' && Previous - 1U > Begin &&
              Masked[Previous - 2U] == '-'))) {
            continue;
        }

        size_t Operator = I;
        while (Operator < End &&
               std::isspace(static_cast<unsigned char>(Masked[Operator]))) {
            ++Operator;
        }
        bool Arrow = Operator + 1U < End && Masked[Operator] == '-' &&
            Masked[Operator + 1U] == '>';
        bool Dot = Operator < End && Masked[Operator] == '.' &&
            !(Operator + 1U < End &&
              std::isdigit(static_cast<unsigned char>(Masked[Operator + 1U])));
        if (!Arrow && !Dot) continue;
        size_t FieldStart = Operator + (Arrow ? 2U : 1U);
        while (FieldStart < End &&
               std::isspace(static_cast<unsigned char>(Masked[FieldStart]))) {
            ++FieldStart;
        }
        size_t FieldEnd = FieldStart;
        while (FieldEnd < End && isIdentifierChar(Masked[FieldEnd])) {
            ++FieldEnd;
        }
        if (FieldEnd == FieldStart) continue;
        llvm::StringRef Field = Masked.slice(FieldStart, FieldEnd);

        std::optional<std::string> Type =
            variableTypeInDeclaration(Declaration, Masked, Name, NameStart);
        if (!Type) continue;
        std::optional<std::string> Parameter =
            structuralParameterForAccess(Declaration, *Type, Arrow);
        if (!Parameter) continue;
        const std::vector<std::string> &Schemas =
            Declaration.StructuralTypes.at(*Parameter);
        std::vector<std::string> Resolved;
        std::vector<StructuralField> Fields =
            structuralFields(Context, Schemas, &Resolved);
        if (Resolved.size() != Schemas.size()) continue;
        bool Known = llvm::any_of(Fields, [&](const StructuralField &F) {
            return F.Name == Field;
        });
        if (Known) continue;
        std::vector<std::string> Names;
        for (const StructuralField &Candidate : Fields) {
            Names.push_back(Candidate.Name);
        }
        Issues.push_back(TemplateIssue{
            FieldStart,
            "tmplgen: field '" + Field.str() + "' is not guaranteed by " +
                *Parameter + " <: " + joinNames(Schemas, " + ") +
                " (available: " + joinNames(Names, ", ") + ")"});
    }
}

// The spelled type of an argument expression, when it can be determined.
std::optional<std::string> argumentType(
    const TemplateDeclaration &Caller, llvm::StringRef Masked,
    llvm::StringRef Argument, size_t Offset)
{
    Argument = Argument.trim();
    while (Argument.starts_with("(") && Argument.ends_with(")")) {
        // Strip only parentheses that wrap the whole argument.
        int Depth = 0;
        bool Wraps = true;
        for (size_t I = 0U; I < Argument.size(); ++I) {
            if (Argument[I] == '(') ++Depth;
            else if (Argument[I] == ')' && --Depth == 0 &&
                     I + 1U != Argument.size()) {
                Wraps = false;
                break;
            }
        }
        if (!Wraps) break;
        Argument = Argument.drop_front().drop_back().trim();
    }
    if (Argument.empty()) return std::nullopt;

    auto IsIdentifier = [](llvm::StringRef Text) {
        return !Text.empty() &&
            !std::isdigit(static_cast<unsigned char>(Text[0])) &&
            llvm::all_of(Text, isIdentifierChar);
    };

    if (Argument.starts_with("&")) {
        llvm::StringRef Inner = Argument.drop_front().trim();
        if (!IsIdentifier(Inner)) return std::nullopt;
        std::optional<std::string> Type =
            variableTypeInDeclaration(Caller, Masked, Inner, Offset);
        if (!Type) return std::nullopt;
        return *Type + " *";
    }
    if (Argument.starts_with("*")) {
        llvm::StringRef Inner = Argument.drop_front().trim();
        if (!IsIdentifier(Inner)) return std::nullopt;
        std::optional<std::string> Type =
            variableTypeInDeclaration(Caller, Masked, Inner, Offset);
        if (!Type) return std::nullopt;
        TypeTokens Tokens = typeTokens(*Type);
        if (Tokens.empty() || Tokens.back() != "*") return std::nullopt;
        Tokens.pop_back();
        return spellTokens(Tokens);
    }
    if (Argument.starts_with("(")) {
        // A cast whose type starts with a template parameter or type word.
        int Depth = 0;
        size_t Close = llvm::StringRef::npos;
        for (size_t I = 0U; I < Argument.size(); ++I) {
            if (Argument[I] == '(') ++Depth;
            else if (Argument[I] == ')' && --Depth == 0) {
                Close = I;
                break;
            }
        }
        if (Close == llvm::StringRef::npos || Close + 1U == Argument.size()) {
            return std::nullopt;
        }
        llvm::StringRef Cast = Argument.slice(1U, Close).trim();
        llvm::StringRef Operand = Argument.drop_front(Close + 1U).ltrim();
        llvm::StringRef First = firstIdentifier(Cast);
        // `(Option(T) *)value` is a cast; `(compute(x)) + 1` is not.
        bool GenericConstructor = !First.empty() &&
            Cast.drop_front(First.size()).ltrim().starts_with("(") &&
            !Operand.empty() &&
            (isIdentifierChar(Operand.front()) || Operand.front() == '(');
        if (First.empty() || !Cast.starts_with(First) ||
            (!GenericConstructor &&
             !isTypeStartWord(First, Caller.Parameters))) {
            return std::nullopt;
        }
        return Cast.str();
    }
    if (IsIdentifier(Argument)) {
        return variableTypeInDeclaration(Caller, Masked, Argument, Offset);
    }
    return std::nullopt;
}

// Checks that `Provided` (the fields a binding is known to have) satisfies the
// structural rows `Required` demanded by a callee.
void checkStructuralSatisfaction(
    llvm::StringRef Callee, llvm::StringRef Variable,
    llvm::ArrayRef<std::string> RequiredSchemas,
    llvm::ArrayRef<StructuralField> Required, llvm::StringRef Binding,
    llvm::StringRef ProvidedDescription,
    llvm::ArrayRef<StructuralField> Provided, size_t Offset,
    std::vector<TemplateIssue> &Issues)
{
    for (const StructuralField &Field : Required) {
        const StructuralField *Match = nullptr;
        for (const StructuralField &Candidate : Provided) {
            if (Candidate.Name == Field.Name) {
                Match = &Candidate;
                break;
            }
        }
        std::string Prefix = "tmplgen: " + Callee.str() + " requires " +
            Variable.str() + " <: " + joinNames(RequiredSchemas, " + ") +
            ", but " + Binding.str();
        if (Match == nullptr) {
            Issues.push_back(TemplateIssue{
                Offset, Prefix + " " + ProvidedDescription.str() +
                            " does not provide field '" + Field.Name +
                            "' (" + Field.Schema + "." + Field.Name + ")"});
        } else if (Match->Canonical != Field.Canonical) {
            Issues.push_back(TemplateIssue{
                Offset, Prefix + " field '" + Field.Name + "' has type '" +
                            Match->Type + "' instead of '" + Field.Type +
                            "' (" + Field.Schema + "." + Field.Name + ")"});
        }
    }
}

void checkTemplateCall(
    const ASTContext &Context, const TemplateDeclaration &Caller,
    const TemplateDeclaration &Callee, llvm::StringRef Masked,
    size_t NameOffset, size_t OpenParen, size_t CloseParen,
    std::vector<TemplateIssue> &Issues)
{
    std::vector<std::pair<llvm::StringRef, size_t>> Arguments;
    {
        size_t Start = OpenParen + 1U;
        int Depth = 0;
        for (size_t I = Start; I <= CloseParen; ++I) {
            char Character = Masked[I];
            if (I == CloseParen || (Character == ',' && Depth == 0)) {
                Arguments.emplace_back(Masked.slice(Start, I), Start);
                Start = I + 1U;
                continue;
            }
            if (Character == '(' || Character == '[' || Character == '{') {
                ++Depth;
            } else if (Character == ')' || Character == ']' ||
                       Character == '}') {
                --Depth;
            }
        }
        if (Arguments.size() == 1U && Arguments[0].first.trim().empty()) {
            Arguments.clear();
        }
    }

    TypeBindings Bindings;
    std::unordered_map<std::string, size_t> BindingOffsets;
    size_t Count = std::min(Arguments.size(), Callee.OrderedParameters.size());
    for (size_t Index = 0U; Index < Count; ++Index) {
        const auto &[Text, Start] = Arguments[Index];
        size_t ArgumentOffset = Start;
        while (ArgumentOffset < Masked.size() &&
               std::isspace(static_cast<unsigned char>(Masked[ArgumentOffset]))) {
            ++ArgumentOffset;
        }
        const auto &[ParameterName, ParameterType] =
            Callee.OrderedParameters[Index];
        if (ParameterType.empty()) continue;
        std::optional<std::string> Type =
            argumentType(Caller, Masked, Text, ArgumentOffset);
        if (!Type) continue;
        TypeTokens Pattern = typeTokens(ParameterType);
        TypeTokens Target = typeTokens(*Type);
        if (Pattern.empty() || Target.empty()) continue;

        TypeBindings Attempt = Bindings;
        if (unifyTypeTokens(Pattern, 0U, Target, 0U, Callee.Parameters,
                            Attempt)) {
            for (const auto &Entry : Attempt) {
                if (!Bindings.count(Entry.first)) {
                    BindingOffsets[Entry.first] = ArgumentOffset;
                }
            }
            Bindings = std::move(Attempt);
            continue;
        }

        bool PatternGeneric = isGenericSpelling(Pattern, {});
        TypeBindings Fresh;
        if (unifyTypeTokens(Pattern, 0U, Target, 0U, Callee.Parameters,
                            Fresh)) {
            for (const auto &Entry : Fresh) {
                auto Previous = Bindings.find(Entry.first);
                if (Previous == Bindings.end() ||
                    Previous->second == Entry.second) {
                    continue;
                }
                if (!isGenericSpelling(Previous->second, Caller.Parameters) &&
                    !isGenericSpelling(Entry.second, Caller.Parameters)) {
                    continue;
                }
                Issues.push_back(TemplateIssue{
                    ArgumentOffset,
                    "tmplgen: " + Callee.Name + " type variable " +
                        Entry.first +
                        " must be consistent across arguments (got " +
                        spellTokens(Previous->second) + " vs " +
                        spellTokens(Entry.second) + ")"});
                break;
            }
            continue;
        }
        if (!PatternGeneric && !isGenericSpelling(Target, Caller.Parameters)) {
            continue;
        }
        Issues.push_back(TemplateIssue{
            ArgumentOffset,
            "tmplgen: " + Callee.Name + " argument " +
                std::to_string(Index + 1U) + " has type '" +
                spellTokens(Target) + "' but parameter '" + ParameterName +
                "' expects '" + spellTokens(Pattern) + "'",
            true});
    }

    // Structural requirements of the callee must follow from what the caller
    // knows about the bound type.
    for (const auto &[Variable, Schemas] : Callee.StructuralTypes) {
        auto Binding = Bindings.find(Variable);
        if (Binding == Bindings.end() || Schemas.empty()) continue;
        std::vector<std::string> Resolved;
        std::vector<StructuralField> Required =
            structuralFields(Context, Schemas, &Resolved);
        if (Resolved.size() != Schemas.size() || Required.empty()) continue;
        const TypeTokens &Bound = Binding->second;
        size_t Offset = BindingOffsets.count(Variable)
            ? BindingOffsets[Variable] : NameOffset;
        std::string BoundSpelling = spellTokens(Bound);

        if (Bound.size() == 1U &&
            llvm::is_contained(Caller.Parameters, Bound.front())) {
            std::vector<StructuralField> Provided;
            std::string Description = "has no structural requirements";
            auto CallerSchemas = Caller.StructuralTypes.find(Bound.front());
            if (CallerSchemas != Caller.StructuralTypes.end() &&
                !CallerSchemas->second.empty()) {
                std::vector<std::string> CallerResolved;
                Provided = structuralFields(Context, CallerSchemas->second,
                                            &CallerResolved);
                if (CallerResolved.size() != CallerSchemas->second.size()) {
                    continue;
                }
                Description = "(<: " +
                    joinNames(CallerSchemas->second, " + ") + ")";
            }
            checkStructuralSatisfaction(Callee.Name, Variable, Schemas,
                                        Required, BoundSpelling, Description,
                                        Provided, Offset, Issues);
            continue;
        }

        // A concrete record type.
        llvm::StringRef RecordName = Bound.back();
        if (Bound.size() > 2U ||
            (Bound.size() == 2U && Bound.front() != "struct" &&
             Bound.front() != "union")) {
            continue;
        }
        const RecordDecl *Record = findRecordDeclaration(Context, RecordName);
        if (Record == nullptr) continue;
        checkStructuralSatisfaction(Callee.Name, Variable, Schemas, Required,
                                    BoundSpelling, "", recordFields(*Record,
                                    RecordName), Offset, Issues);
    }
}

void checkTemplateCalls(
    const ASTContext &Context, const TemplateDeclaration &Caller,
    const std::unordered_map<std::string, const TemplateDeclaration *> &Callees,
    llvm::StringRef Masked, std::vector<TemplateIssue> &Issues)
{
    size_t Begin = Caller.BodyOpenOffset + 1U;
    size_t End = Caller.BodyCloseOffset;
    for (size_t I = Begin; I < End;) {
        if (!isIdentifierChar(Masked[I]) ||
            std::isdigit(static_cast<unsigned char>(Masked[I]))) {
            ++I;
            continue;
        }
        size_t NameStart = I;
        while (I < End && isIdentifierChar(Masked[I])) ++I;
        auto Callee = Callees.find(Masked.slice(NameStart, I).str());
        if (Callee == Callees.end()) continue;
        if (NameStart > Begin &&
            (Masked[NameStart - 1U] == '.' || Masked[NameStart - 1U] == '>')) {
            continue;
        }
        size_t Open = I;
        while (Open < End &&
               std::isspace(static_cast<unsigned char>(Masked[Open]))) {
            ++Open;
        }
        if (Open >= End || Masked[Open] != '(') continue;
        int Depth = 0;
        size_t Close = llvm::StringRef::npos;
        for (size_t J = Open; J < End; ++J) {
            if (Masked[J] == '(') ++Depth;
            else if (Masked[J] == ')' && --Depth == 0) {
                Close = J;
                break;
            }
        }
        if (Close == llvm::StringRef::npos) continue;
        checkTemplateCall(Context, Caller, *Callee->second, Masked, NameStart,
                          Open, Close, Issues);
    }
}

std::vector<TemplateIssue> checkTemplateBodies(
    const ASTContext &Context, const TemplateModel &Model,
    llvm::ArrayRef<const TemplateModel *> OtherModels, llvm::StringRef Code)
{
    std::unordered_map<std::string, const TemplateDeclaration *> Callees;
    for (const TemplateModel *Other : OtherModels) {
        for (const TemplateDeclaration &Declaration : Other->Declarations) {
            if (Declaration.IsFunction && !Declaration.Name.empty()) {
                Callees[Declaration.Name] = &Declaration;
            }
        }
    }
    for (const TemplateDeclaration &Declaration : Model.Declarations) {
        if (Declaration.IsFunction && !Declaration.Name.empty()) {
            Callees[Declaration.Name] = &Declaration;
        }
    }

    std::string Masked = maskCommentsAndLiterals(Code);
    std::vector<TemplateIssue> Issues;
    for (const TemplateDeclaration &Declaration : Model.Declarations) {
        if (!Declaration.IsFunction ||
            Declaration.BodyOpenOffset == llvm::StringRef::npos ||
            Declaration.BodyCloseOffset >= Masked.size()) {
            continue;
        }
        checkStructuralFieldAccesses(Context, Declaration, Masked, Issues);
        checkTemplateCalls(Context, Declaration, Callees, Masked, Issues);
    }
    return Issues;
}

std::vector<Location> constraintDefinitionLocations(
    ParsedAST &AST, const TextDocumentPositionParams &Params,
    llvm::StringRef MainPath)
{
    const SourceManager &SourceMgr = AST.getSourceManager();
    FileID MainFile = SourceMgr.getMainFileID();
    if (MainFile.isInvalid()) return {};
    llvm::StringRef Code =
        SourceMgr.getBufferOrFake(MainFile).getBuffer();
    llvm::Expected<size_t> Offset =
        positionToOffset(Code, Params.position, false);
    if (!Offset) {
        llvm::consumeError(Offset.takeError());
        return {};
    }

    size_t Cursor = *Offset;
    size_t LineStart = 0U;
    if (Cursor > 0U) {
        size_t Newline = Code.rfind('\n', Cursor - 1U);
        if (Newline != llvm::StringRef::npos) LineStart = Newline + 1U;
    }
    size_t LineEnd = Code.find('\n', Cursor);
    if (LineEnd == llvm::StringRef::npos) LineEnd = Code.size();
    llvm::StringRef Line = Code.slice(LineStart, LineEnd);
    std::optional<std::pair<llvm::StringRef, size_t>> Word =
        constraintWordAt(Line, Cursor - LineStart);
    if (!Word) return {};

    SourceLocation UseLoc = SourceMgr
        .getLocForStartOfFile(MainFile)
        .getLocWithOffset(LineStart + Word->second);
    if (std::optional<Location> Macro = macroDefinitionLocation(
            AST, Word->first, UseLoc, MainPath)) {
        return {std::move(*Macro)};
    }
    const NamedDecl *Type = findTypeDeclaration(
        *AST.getASTContext().getTranslationUnitDecl(), Word->first);
    if (Type == nullptr) return {};
    if (std::optional<Location> Declaration = sourceLocationToDefinition(
            SourceMgr, AST.getLangOpts(), Type->getLocation(), MainPath)) {
        return {std::move(*Declaration)};
    }
    return {};
}

std::optional<Location> templateFunctionLocation(
    const LocatedSymbol &Symbol, const Location &GeneratedLocation,
    llvm::StringRef MainPath)
{
    auto GeneratedBuffer = llvm::MemoryBuffer::getFile(
        GeneratedLocation.uri.file());
    if (!GeneratedBuffer) return std::nullopt;
    llvm::SmallVector<llvm::StringRef, 256> Lines;
    (*GeneratedBuffer)->getBuffer().split(Lines, '\n');
    size_t TargetLine = GeneratedLocation.range.start.line;
    if (TargetLine >= Lines.size()) return std::nullopt;

    llvm::StringRef MacroLine = Lines[TargetLine].trim();
    if (!MacroLine.consume_front("#define")) return std::nullopt;
    MacroLine = MacroLine.ltrim();
    llvm::StringRef MacroName = MacroLine.take_while([](char C) {
        return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
    });
    if (MacroName != Symbol.Name) return std::nullopt;

    size_t TemplateLine = 0U;
    std::optional<std::string> TemplatePath;
    size_t LowerBound = TargetLine > 16U ? TargetLine - 16U : 0U;
    for (size_t I = TargetLine; I > LowerBound; --I) {
        llvm::StringRef Candidate = Lines[I - 1U].trim();
        if (!Candidate.consume_front("#line ")) continue;
        Candidate = Candidate.ltrim();
        size_t Digits = Candidate.find_first_not_of("0123456789");
        if (Digits == llvm::StringRef::npos || Digits == 0U) continue;
        unsigned ParsedLine = 0U;
        if (Candidate.take_front(Digits).getAsInteger(10, ParsedLine) ||
            ParsedLine == 0U) {
            continue;
        }
        TemplatePath = decodeLineDirectivePath(Candidate.drop_front(Digits));
        if (TemplatePath) {
            TemplatePath = resolveLineDirectivePath(
                *TemplatePath, GeneratedLocation.uri.file());
            TemplateLine = ParsedLine;
            break;
        }
    }
    if (!TemplatePath || TemplateLine == 0U) return std::nullopt;

    auto TemplateBuffer = llvm::MemoryBuffer::getFile(*TemplatePath);
    if (!TemplateBuffer) return std::nullopt;
    llvm::SmallVector<llvm::StringRef, 256> TemplateLines;
    (*TemplateBuffer)->getBuffer().split(TemplateLines, '\n');
    if (TemplateLine > TemplateLines.size()) return std::nullopt;
    llvm::StringRef Head = TemplateLines[TemplateLine - 1U];
    size_t NameStart = Head.find(Symbol.Name);
    if (NameStart == llvm::StringRef::npos) return std::nullopt;
    size_t NameEnd = NameStart + Symbol.Name.size();
    if ((NameStart > 0U &&
         (std::isalnum(static_cast<unsigned char>(Head[NameStart - 1U])) ||
          Head[NameStart - 1U] == '_')) ||
        (NameEnd < Head.size() &&
         (std::isalnum(static_cast<unsigned char>(Head[NameEnd])) ||
          Head[NameEnd] == '_'))) {
        return std::nullopt;
    }
    return Location{
        URIForFile::canonicalize(*TemplatePath, MainPath),
        Range{{static_cast<int>(TemplateLine - 1U),
               static_cast<int>(NameStart)},
              {static_cast<int>(TemplateLine - 1U),
               static_cast<int>(NameEnd)}}};
}

bool isToggleTarget(const TextDocumentPositionParams &Params,
                    const LocatedSymbol &Symbol,
                    Location &Toggle)
{
    if (!Symbol.Definition || *Symbol.Definition == Symbol.PreferredDeclaration)
        return false;
    if (Symbol.Definition->uri.file() == Params.textDocument.uri.file() &&
        Symbol.Definition->range.contains(Params.position)) {
        Toggle = Symbol.PreferredDeclaration;
        return true;
    }
    if (Symbol.PreferredDeclaration.uri.file() == Params.textDocument.uri.file() &&
        Symbol.PreferredDeclaration.range.contains(Params.position)) {
        Toggle = *Symbol.Definition;
        return true;
    }
    return false;
}

struct SemanticHighlight {
    Range R;
    unsigned TokenType;
    uint32_t Modifiers = 0;

    bool operator<(const SemanticHighlight &Other) const
    {
        return std::make_tuple(R.start.line, R.start.character, R.end.line,
                               R.end.character, TokenType, Modifiers) <
               std::make_tuple(Other.R.start.line, Other.R.start.character,
                               Other.R.end.line, Other.R.end.character,
                               Other.TokenType, Other.Modifiers);
    }
};

unsigned ensureKeywordTokenType(llvm::json::Object &ServerCaps)
{
    unsigned Fallback =
        static_cast<unsigned>(HighlightingKind::LastKind) + 1U;
    llvm::json::Object *Provider =
        ServerCaps.getObject("semanticTokensProvider");
    if (Provider == nullptr) return Fallback;
    llvm::json::Object *Legend = Provider->getObject("legend");
    if (Legend == nullptr) return Fallback;
    llvm::json::Array *Types = Legend->getArray("tokenTypes");
    if (Types == nullptr) return Fallback;
    for (size_t I = 0U; I < Types->size(); ++I) {
        std::optional<llvm::StringRef> Name = (*Types)[I].getAsString();
        if (Name && *Name == "keyword") return static_cast<unsigned>(I);
    }
    Types->push_back("keyword");
    return static_cast<unsigned>(Types->size() - 1U);
}

void addAnnotationToken(std::vector<SemanticHighlight> &Tokens,
                        size_t LineNumber, llvm::StringRef Line,
                        size_t Start, size_t End, unsigned TokenType)
{
    if (Start >= Line.size() || End <= Start) return;
    End = std::min(End, Line.size());
    unsigned StartColumn = lspLength(Line.take_front(Start));
    unsigned EndColumn = StartColumn + lspLength(Line.slice(Start, End));
    Tokens.push_back(SemanticHighlight{
        Range{{static_cast<int>(LineNumber), static_cast<int>(StartColumn)},
              {static_cast<int>(LineNumber), static_cast<int>(EndColumn)}},
        TokenType, 0U});
}

bool isBuiltinTypeName(llvm::StringRef Name)
{
    for (llvm::StringRef Builtin : {
             "void", "_Bool", "bool", "char", "signed", "unsigned", "short",
             "int", "long", "float", "double", "_Complex", "_Imaginary",
             "__int128", "size_t", "ptrdiff_t", "nullptr_t"}) {
        if (Name == Builtin) return true;
    }
    return false;
}

bool isConstraintKeyword(llvm::StringRef Name)
{
    for (llvm::StringRef Keyword : {
             "sizeof", "_Alignof", "alignof", "typeof", "__typeof",
             "__typeof__", "const", "volatile", "restrict", "_Generic"}) {
        if (Name == Keyword) return true;
    }
    return false;
}

bool looksLikeMacroName(llvm::StringRef Name)
{
    if (Name.size() < 2U) return false;
    bool HasLetter = false;
    for (char Character : Name) {
        if (std::isalpha(static_cast<unsigned char>(Character))) {
            if (std::islower(static_cast<unsigned char>(Character))) return false;
            HasLetter = true;
        } else if (Character != '_' &&
                   !std::isdigit(static_cast<unsigned char>(Character))) {
            return false;
        }
    }
    return HasLetter;
}

unsigned classifyAnnotationIdentifier(ParsedAST &AST, llvm::StringRef Name,
                                      unsigned KeywordTokenType)
{
    unsigned TypeToken = static_cast<unsigned>(HighlightingKind::Type);
    unsigned MacroToken = static_cast<unsigned>(HighlightingKind::Macro);
    unsigned ParameterToken =
        static_cast<unsigned>(HighlightingKind::TemplateParameter);
    if (isBuiltinTypeName(Name)) return TypeToken;
    if (isConstraintKeyword(Name)) return KeywordTokenType;
    Preprocessor &PP = AST.getPreprocessor();
    IdentifierInfo *Identifier = PP.getIdentifierInfo(Name);
    if (Identifier != nullptr && Identifier->hadMacroDefinition()) {
        return MacroToken;
    }
    if (Name.size() == 1U &&
        std::isupper(static_cast<unsigned char>(Name[0]))) {
        return ParameterToken;
    }
    if (findTypeDeclaration(*AST.getASTContext().getTranslationUnitDecl(),
                            Name) != nullptr) {
        return TypeToken;
    }
    if (looksLikeMacroName(Name)) return MacroToken;
    return TypeToken;
}

void addParameterTokens(std::vector<SemanticHighlight> &Tokens,
                        size_t LineNumber, llvm::StringRef Line,
                        size_t Begin, size_t End)
{
    unsigned ParameterToken =
        static_cast<unsigned>(HighlightingKind::TemplateParameter);
    End = std::min(End, Line.size());
    for (size_t I = Begin; I < End;) {
        if (!isIdentifierChar(Line[I])) {
            ++I;
            continue;
        }
        size_t Start = I++;
        while (I < End && isIdentifierChar(Line[I])) ++I;
        addAnnotationToken(Tokens, LineNumber, Line, Start, I,
                           ParameterToken);
    }
}

void addExpressionTokens(std::vector<SemanticHighlight> &Tokens,
                         ParsedAST &AST, size_t LineNumber,
                         llvm::StringRef Line, size_t Begin, size_t End,
                         unsigned KeywordTokenType)
{
    static const llvm::StringRef Operators[] = {
        "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&",
        "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "+",
        "-",  "*",  "/",  "%",  "<",  ">",  "=",  "!",  "&",  "|",
        "^",  "~",  "?"};
    unsigned OperatorToken =
        static_cast<unsigned>(HighlightingKind::Operator);
    End = std::min(End, Line.size());
    for (size_t I = Begin; I < End;) {
        if (isIdentifierChar(Line[I])) {
            size_t Start = I++;
            while (I < End && isIdentifierChar(Line[I])) ++I;
            addAnnotationToken(
                Tokens, LineNumber, Line, Start, I,
                classifyAnnotationIdentifier(
                    AST, Line.slice(Start, I), KeywordTokenType));
            continue;
        }
        bool MatchedOperator = false;
        for (llvm::StringRef Operator : Operators) {
            if (I + Operator.size() > End) continue;
            if (Line.substr(I, Operator.size()) != Operator) continue;
            addAnnotationToken(Tokens, LineNumber, Line, I,
                               I + Operator.size(), OperatorToken);
            I += Operator.size();
            MatchedOperator = true;
            break;
        }
        if (!MatchedOperator) ++I;
    }
}

struct ConstraintOperator {
    size_t Start;
    size_t End;
};

std::optional<ConstraintOperator> findConstraintOperator(
    llvm::StringRef Line, size_t Begin)
{
    for (size_t I = Begin; I + 1U < Line.size(); ++I) {
        llvm::StringRef Candidate = Line.substr(I, 2U);
        if (Candidate == "<:" || Candidate == "|=") {
            return ConstraintOperator{I, I + 2U};
        }
    }
    return std::nullopt;
}

size_t expressionEnd(llvm::StringRef Line, size_t Begin)
{
    int Depth = 0;
    for (size_t I = Begin; I < Line.size(); ++I) {
        char Character = Line[I];
        if (Character == '(' || Character == '[' || Character == '{') {
            ++Depth;
        } else if (Character == ')' || Character == ']' || Character == '}') {
            if (Depth > 0) --Depth;
        } else if (Character == ',' && Depth == 0) {
            return I;
        }
    }
    return Line.size();
}

// Tokens inside an @meta initializer list: field designators are fields,
// brackets and operators get their own kinds, other identifiers are
// classified, and literals are left to the editor's syntax grammar.
void addMetaInitializerTokens(std::vector<SemanticHighlight> &Tokens,
                              ParsedAST &AST, size_t LineNumber,
                              llvm::StringRef Line, size_t Begin, size_t End,
                              unsigned KeywordTokenType)
{
    static const llvm::StringRef Operators[] = {
        "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&",
        "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "+",
        "-",  "*",  "/",  "%",  "<",  ">",  "=",  "!",  "&",  "|",
        "^",  "~",  "?"};
    unsigned OperatorToken =
        static_cast<unsigned>(HighlightingKind::Operator);
    unsigned FieldToken = static_cast<unsigned>(HighlightingKind::Field);
    unsigned BracketToken = static_cast<unsigned>(HighlightingKind::Bracket);
    End = std::min(End, Line.size());
    for (size_t I = Begin; I < End;) {
        char Character = Line[I];
        if (Character == '{' || Character == '}' || Character == '(' ||
            Character == ')' || Character == '[' || Character == ']') {
            addAnnotationToken(Tokens, LineNumber, Line, I, I + 1U,
                               BracketToken);
            ++I;
            continue;
        }
        if (Character == '.' && I + 1U < End &&
            (std::isalpha(static_cast<unsigned char>(Line[I + 1U])) ||
             Line[I + 1U] == '_')) {
            size_t Start = I + 1U;
            size_t Stop = Start;
            while (Stop < End && isIdentifierChar(Line[Stop])) ++Stop;
            addAnnotationToken(Tokens, LineNumber, Line, Start, Stop,
                               FieldToken);
            I = Stop;
            continue;
        }
        if (Character == '"' || Character == '\'') {
            char Quote = Character;
            size_t Stop = I + 1U;
            while (Stop < End && Line[Stop] != Quote) {
                if (Line[Stop] == '\\' && Stop + 1U < End) ++Stop;
                ++Stop;
            }
            I = Stop < End ? Stop + 1U : Stop;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(Character))) {
            while (I < End &&
                   (std::isalnum(static_cast<unsigned char>(Line[I])) ||
                    Line[I] == '.' || Line[I] == '_' || Line[I] == '+' ||
                    Line[I] == '-')) {
                ++I;
            }
            continue;
        }
        if (isIdentifierChar(Character)) {
            size_t Start = I++;
            while (I < End && isIdentifierChar(Line[I])) ++I;
            addAnnotationToken(
                Tokens, LineNumber, Line, Start, I,
                classifyAnnotationIdentifier(
                    AST, Line.slice(Start, I), KeywordTokenType));
            continue;
        }
        bool MatchedOperator = false;
        for (llvm::StringRef Operator : Operators) {
            if (I + Operator.size() > End) continue;
            if (Line.substr(I, Operator.size()) != Operator) continue;
            addAnnotationToken(Tokens, LineNumber, Line, I,
                               I + Operator.size(), OperatorToken);
            I += Operator.size();
            MatchedOperator = true;
            break;
        }
        if (!MatchedOperator) ++I;
    }
}

void highlightAnnotationLine(std::vector<SemanticHighlight> &Tokens,
                             ParsedAST &AST, size_t LineNumber,
                             llvm::StringRef Line,
                             unsigned KeywordTokenType)
{
    size_t Comment = Line.find("//");
    if (Comment == llvm::StringRef::npos) return;
    llvm::StringRef CommentText = Line.drop_front(Comment + 2U);
    size_t At = CommentText.find('@');
    if (At == llvm::StringRef::npos) return;
    llvm::StringRef Directive = CommentText.drop_front(At);
    size_t DirectiveStart = Comment + 2U + At;

    llvm::StringRef Template = "@template";
    if (Directive.starts_with(Template) &&
        (Directive.size() == Template.size() ||
         !isIdentifierChar(Directive[Template.size()]))) {
        addAnnotationToken(Tokens, LineNumber, Line, DirectiveStart,
                           DirectiveStart + Template.size(),
                           KeywordTokenType);
        size_t Position = DirectiveStart + Template.size();
        while (Position < Line.size()) {
            std::optional<ConstraintOperator> Operator =
                findConstraintOperator(Line, Position);
            if (!Operator) {
                addParameterTokens(Tokens, LineNumber, Line, Position,
                                   Line.size());
                break;
            }
            addParameterTokens(Tokens, LineNumber, Line, Position,
                               Operator->Start);
            addAnnotationToken(
                Tokens, LineNumber, Line, Operator->Start, Operator->End,
                static_cast<unsigned>(HighlightingKind::Operator));
            size_t End = expressionEnd(Line, Operator->End);
            addExpressionTokens(Tokens, AST, LineNumber, Line,
                                Operator->End, End, KeywordTokenType);
            Position = End;
            if (Position < Line.size() && Line[Position] == ',') {
                ++Position;
            } else {
                break;
            }
        }
        return;
    }

    llvm::StringRef Where = "@where";
    if (Directive.starts_with(Where) &&
        (Directive.size() == Where.size() ||
         !isIdentifierChar(Directive[Where.size()]))) {
        addAnnotationToken(Tokens, LineNumber, Line, DirectiveStart,
                           DirectiveStart + Where.size(), KeywordTokenType);
        addExpressionTokens(Tokens, AST, LineNumber, Line,
                            DirectiveStart + Where.size(), Line.size(),
                            KeywordTokenType);
        return;
    }

    // `// @meta (FieldInfo){.key = "data"}`: the annotation is a compound
    // literal, so the parenthesized type is one type token (no per-name
    // classification) and the initializer is fields, brackets and operators.
    llvm::StringRef Meta = "@meta";
    if (Directive.starts_with(Meta) &&
        (Directive.size() == Meta.size() ||
         !isIdentifierChar(Directive[Meta.size()]))) {
        addAnnotationToken(Tokens, LineNumber, Line, DirectiveStart,
                           DirectiveStart + Meta.size(), KeywordTokenType);
        size_t Begin = DirectiveStart + Meta.size();
        while (Begin < Line.size() &&
               std::isspace(static_cast<unsigned char>(Line[Begin]))) ++Begin;
        if (Begin >= Line.size() || Line[Begin] != '(') return;
        int Depth = 0;
        size_t Close = llvm::StringRef::npos;
        for (size_t I = Begin; I < Line.size(); ++I) {
            if (Line[I] == '(') {
                ++Depth;
            } else if (Line[I] == ')' && --Depth == 0) {
                Close = I;
                break;
            }
        }
        if (Close == llvm::StringRef::npos) return;
        unsigned BracketToken =
            static_cast<unsigned>(HighlightingKind::Bracket);
        addAnnotationToken(Tokens, LineNumber, Line, Begin, Begin + 1U,
                           BracketToken);
        size_t TypeStart = Begin + 1U;
        size_t TypeEnd = Close;
        while (TypeStart < TypeEnd &&
               std::isspace(static_cast<unsigned char>(Line[TypeStart]))) {
            ++TypeStart;
        }
        while (TypeEnd > TypeStart &&
               std::isspace(static_cast<unsigned char>(Line[TypeEnd - 1U]))) {
            --TypeEnd;
        }
        addAnnotationToken(Tokens, LineNumber, Line, TypeStart, TypeEnd,
                           static_cast<unsigned>(HighlightingKind::Type));
        addAnnotationToken(Tokens, LineNumber, Line, Close, Close + 1U,
                           BracketToken);
        size_t Brace = Close + 1U;
        while (Brace < Line.size() &&
               std::isspace(static_cast<unsigned char>(Line[Brace]))) ++Brace;
        if (Brace < Line.size() && Line[Brace] == '{') {
            addMetaInitializerTokens(Tokens, AST, LineNumber, Line, Brace,
                                     Line.size(), KeywordTokenType);
        }
    }
}

void collectAnnotationTokens(std::vector<SemanticHighlight> &Tokens,
                             ParsedAST &AST, llvm::StringRef Path,
                             llvm::StringRef Code,
                             unsigned KeywordTokenType)
{
    if (!Path.ends_with(".tmpl.h") && !Path.ends_with(".tmpl.c")) return;
    size_t LineStart = 0U;
    size_t LineNumber = 0U;
    while (LineStart < Code.size()) {
        size_t LineEnd = Code.find('\n', LineStart);
        if (LineEnd == llvm::StringRef::npos) LineEnd = Code.size();
        highlightAnnotationLine(Tokens, AST, LineNumber,
                                Code.slice(LineStart, LineEnd),
                                KeywordTokenType);
        if (LineEnd == Code.size()) break;
        LineStart = LineEnd + 1U;
        ++LineNumber;
    }
}

std::vector<SemanticToken> encodeSemanticHighlights(
    llvm::ArrayRef<SemanticHighlight> Highlights, llvm::StringRef Code)
{
    std::vector<SemanticToken> Result;
    const SemanticHighlight *Last = nullptr;
    SemanticHighlight Scratch;
    for (const SemanticHighlight &Token : Highlights) {
        Result.emplace_back();
        SemanticToken *Out = &Result.back();
        if (Last != nullptr) {
            Out->deltaLine = Token.R.start.line - Last->R.end.line;
            if (Out->deltaLine == 0U) {
                Out->deltaStart =
                    Token.R.start.character - Last->R.start.character;
            } else {
                Out->deltaStart = Token.R.start.character;
            }
        } else {
            Out->deltaLine = Token.R.start.line;
            Out->deltaStart = Token.R.start.character;
        }
        Out->tokenType = Token.TokenType;
        Out->tokenModifiers = Token.Modifiers;
        Last = &Token;

        if (Token.R.end.line == Token.R.start.line) {
            Out->length = Token.R.end.character - Token.R.start.character;
            continue;
        }

        auto TokenStart = llvm::cantFail(
            positionToOffset(Code, Token.R.start));
        for (int I = Token.R.start.line; I < Token.R.end.line; ++I) {
            size_t LineEnd = Code.find('\n', TokenStart);
            if (LineEnd == llvm::StringRef::npos) break;
            Out->length = static_cast<unsigned>(LineEnd - TokenStart);
            TokenStart = LineEnd + 1U;
            Result.emplace_back();
            Out = &Result.back();
            *Out = Result[Result.size() - 2U];
            Out->deltaLine = 1U;
            Out->deltaStart = 0U;
        }
        Out->length = static_cast<unsigned>(Token.R.end.character);
        Scratch = *Last;
        Scratch.R.start.line = Token.R.end.line;
        Scratch.R.start.character = 0;
        Last = &Scratch;
    }
    return Result;
}

std::vector<SemanticToken> semanticTokensForAST(
    ParsedAST &AST, llvm::StringRef Path, llvm::StringRef Code,
    unsigned KeywordTokenType)
{
    std::vector<HighlightingToken> Base =
        getSemanticHighlightings(AST, true);
    std::vector<SemanticHighlight> Highlights;
    Highlights.reserve(Base.size() + 16U);
    for (const HighlightingToken &Token : Base) {
        Highlights.push_back(
            SemanticHighlight{Token.R,
                              static_cast<unsigned>(Token.Kind),
                              Token.Modifiers});
    }
    collectAnnotationTokens(Highlights, AST, Path, Code, KeywordTokenType);
    llvm::sort(Highlights);
    return encodeSemanticHighlights(Highlights, Code);
}

void incrementResultId(std::string &ResultId)
{
    for (char &Character : llvm::reverse(ResultId)) {
        if (Character != '9') {
            ++Character;
            return;
        }
        Character = '0';
    }
    ResultId.insert(ResultId.begin(), '1');
}

CompletionList renderCompletions(const CodeCompleteResult &Result,
                                 const CodeCompleteOptions &Options)
{
    CompletionList List;
    List.isIncomplete = Result.HasMore;
    List.items.reserve(Result.Completions.size());
    for (const CodeCompletion &Completion : Result.Completions) {
        List.items.push_back(Completion.render(Options));
    }
    return List;
}

// Completion for `variable->` / `variable.` on a structurally constrained
// template parameter. The fallback type is meaningless here, so only the
// fields guaranteed by the structural rows are offered.
std::optional<CompletionList> structuralCompletions(
    ParsedAST &AST, const StructuralAccessContext &Access,
    llvm::StringRef Code, Position Position)
{
    const std::vector<std::string> &Schemas =
        Access.Declaration->StructuralTypes.at(Access.Parameter);
    std::vector<std::string> Resolved;
    std::vector<StructuralField> Fields =
        structuralFields(AST.getASTContext(), Schemas, &Resolved);
    if (Fields.empty()) return std::nullopt;

    Range WordRange{offsetToPosition(Code, Access.WordStart), Position};
    llvm::Expected<size_t> Cursor = positionToOffset(Code, Position, false);
    if (!Cursor) {
        llvm::consumeError(Cursor.takeError());
        return std::nullopt;
    }
    llvm::StringRef Prefix = Code.slice(Access.WordStart, *Cursor);
    CompletionList List;
    List.isIncomplete = false;
    for (const StructuralField &Field : Fields) {
        if (!llvm::StringRef(Field.Name).starts_with_insensitive(Prefix)) {
            continue;
        }
        CompletionItem Item;
        Item.label = Field.Name;
        Item.filterText = Field.Name;
        Item.sortText = Field.Name;
        Item.kind = CompletionItemKind::Field;
        Item.detail = Field.Type;
        Item.documentation = MarkupContent{
            MarkupKind::PlainText,
            "Structural field of " + Access.Parameter + " <: " +
                joinNames(Schemas, " + ") + " (from " + Field.Schema + ")"};
        Item.textEdit = TextEdit{WordRange, Field.Name};
        Item.score = 1.0f;
        List.items.push_back(std::move(Item));
    }
    return List;
}

class TmplgenASTListener final : public FeatureModule::ASTListener {
public:
    void beforeExecute(clang::CompilerInstance &compiler) override
    {
        const clang::SourceManager &sourceManager =
            compiler.getSourceManager();
        clang::FileID mainFile = sourceManager.getMainFileID();
        bool invalid = true;
        llvm::StringRef contents;
        std::string path;
        if (mainFile.isValid()) {
            contents = sourceManager.getBufferData(mainFile, &invalid);
            path = sourceManager.getFilename(
                sourceManager.getLocForStartOfFile(mainFile)).str();
        }

        // A preamble build only sees the main file up to the end of its
        // leading directives. Handing that truncated text to the generator
        // makes it drop every specialization requested (or template defined)
        // in the rest of the file, and the preamble would then capture the
        // stripped generated headers. Use the text of the last full build of
        // this file instead, or the file on disk on first open.
        bool preambleBuild = compiler.getPreprocessorOpts().GeneratePreamble;
        std::optional<std::string> lastContents;
        if (!path.empty()) {
            std::lock_guard<std::mutex> lock(MainContentsMutex);
            if (preambleBuild) {
                auto found = LastMainContents.find(path);
                if (found != LastMainContents.end()) {
                    lastContents = found->second;
                }
            } else if (!invalid) {
                LastMainContents[path] = contents.str();
            }
        }

        {
            std::lock_guard<std::mutex> lock(GeneratorMutex);
            using tmplgen_plugin_support::MainFileText;
            int result = !preambleBuild
                ? tmplgen_plugin_support::runTmplgen(compiler, {}, true)
                : lastContents
                ? tmplgen_plugin_support::runTmplgen(
                      compiler, {}, true, MainFileText::Override,
                      *lastContents)
                : tmplgen_plugin_support::runTmplgen(
                      compiler, {}, true, MainFileText::Disk);
            if (result != 0) {
                unsigned diagnostic =
                    compiler.getDiagnostics().getCustomDiagID(
                        clang::DiagnosticsEngine::Error,
                        "tmplgen could not update generated files; see the "
                        "clangd log");
                compiler.getDiagnostics().Report(diagnostic);
            }
        }

        if (preambleBuild || invalid || !isTemplateSourcePath(path)) return;
        Model = parseTemplateModel(contents);
        Masked = maskCommentsAndLiterals(contents);
        HasTemplateModel = true;
    }

    void afterExecute(clang::CompilerInstance &compiler) override
    {
        if (!HasTemplateModel || !Model) return;
        const clang::SourceManager &sourceManager =
            compiler.getSourceManager();
        clang::FileID mainFile = sourceManager.getMainFileID();
        if (mainFile.isInvalid()) return;
        bool invalid = false;
        llvm::StringRef contents =
            sourceManager.getBufferData(mainFile, &invalid);
        if (invalid) return;
        llvm::StringRef path = sourceManager.getFilename(
            sourceManager.getLocForStartOfFile(mainFile));

        // Templates from directly included template files can be called too.
        std::vector<std::unique_ptr<TemplateModel>> includedModels;
        llvm::SmallString<256> directory(path);
        llvm::sys::path::remove_filename(directory);
        for (const SourceLineRecord &line : splitSourceLines(contents)) {
            llvm::StringRef text = line.Text.trim();
            if (!text.consume_front("#")) continue;
            text = text.ltrim();
            if (!text.consume_front("include")) continue;
            text = text.ltrim();
            if (!text.consume_front("\"")) continue;
            llvm::StringRef include = text.take_until(
                [](char c) { return c == '"'; });
            if (!isTemplateSourcePath(include)) continue;
            llvm::SmallString<256> candidate(directory);
            llvm::sys::path::append(candidate, include);
            auto buffer =
                compiler.getFileManager().getBufferForFile(candidate);
            if (!buffer) continue;
            includedModels.push_back(std::make_unique<TemplateModel>(
                parseTemplateModel((*buffer)->getBuffer())));
        }
        std::vector<const TemplateModel *> others;
        for (const auto &model : includedModels) others.push_back(model.get());

        std::vector<TemplateIssue> issues = checkTemplateBodies(
            compiler.getASTContext(), *Model, others, contents);
        if (issues.empty()) return;
        clang::DiagnosticsEngine &diagnostics = compiler.getDiagnostics();
        unsigned id = diagnostics.getCustomDiagID(
            clang::DiagnosticsEngine::Error, "%0");
        clang::SourceLocation start =
            sourceManager.getLocForStartOfFile(mainFile);
        for (const TemplateIssue &issue : issues) {
            if (issue.DeferToClang &&
                ClangDiagnosticOffsets.count(
                    static_cast<unsigned>(issue.Offset))) {
                continue;
            }
            diagnostics.Report(
                start.getLocWithOffset(static_cast<int>(issue.Offset)), id)
                << issue.Message;
        }
    }

    void sawDiagnostic(const clang::Diagnostic &diagnostic,
                       clangd::Diag &diag) override
    {
        const clang::SourceManager &sourceManager =
            diagnostic.getSourceManager();
        clang::PresumedLoc presumed = sourceManager.getPresumedLoc(
            diagnostic.getLocation());
        if (!presumed.isValid()) return;
        clang::FileID mainFile = sourceManager.getMainFileID();
        // Macro locations (e.g. `Option(T)` or `Range(int)`) count as the
        // place they were expanded.
        clang::SourceLocation location = diagnostic.getLocation().isValid()
            ? sourceManager.getExpansionLoc(diagnostic.getLocation())
            : diagnostic.getLocation();
        bool inMainFile = mainFile.isValid() && location.isValid() &&
            sourceManager.getFileID(location) == mainFile;
        if (inIncludedTemplateFallback(sourceManager,
                                       diagnostic.getLocation(), mainFile)) {
            diag.Severity = clang::DiagnosticsEngine::Ignored;
            return;
        }
        if (HasTemplateModel && Model && inMainFile &&
            presumed.getLine() != 0U) {
            size_t line = presumed.getLine() - 1U;
            if (Model->shouldSuppressDiagnostic(line, diag.Message) ||
                isDerivedFromTemplateParameter(line, diag.Message)) {
                diag.Severity = clang::DiagnosticsEngine::Ignored;
                return;
            }
            if (diag.Severity >= clang::DiagnosticsEngine::Warning) {
                ClangDiagnosticOffsets.insert(
                    sourceManager.getFileOffset(location));
            }
        }
        if (mainFile.isInvalid() || inMainFile) return;
        llvm::StringRef mainPath = sourceManager.getFilename(
            sourceManager.getLocForStartOfFile(mainFile));
        if (presumed.getLine() == 0U) return;
        // The physical file holding the #line directive (a generated header).
        llvm::StringRef containingFile =
            sourceManager.getFilename(location);
        std::string presumedPath = resolveLineDirectivePath(
            presumed.getFilename(), containingFile);
        if (mainPath != presumedPath) return;
        diag.File = presumedPath;
        diag.AbsFile = diag.File;
        diag.InsideMainFile = true;
        int line = static_cast<int>(presumed.getLine() - 1U);
        diag.Range = {{line, 0}, {line, 0}};
    }

private:
    // Fallback blocks of *other* template files (e.g. a .tmpl.h including
    // another one) are stand-ins as well; their diagnostics are noise.
    bool inIncludedTemplateFallback(const clang::SourceManager &sourceManager,
                                    clang::SourceLocation location,
                                    clang::FileID mainFile)
    {
        if (location.isInvalid()) return false;
        clang::SourceLocation expansion =
            sourceManager.getExpansionLoc(location);
        clang::FileID file = sourceManager.getFileID(expansion);
        if (file.isInvalid() || file == mainFile) return false;
        auto cached = IncludedFallbacks.find(file);
        if (cached == IncludedFallbacks.end()) {
            std::vector<TemplateLineRange> ranges;
            llvm::StringRef name = sourceManager.getFilename(expansion);
            bool invalid = false;
            llvm::StringRef contents =
                sourceManager.getBufferData(file, &invalid);
            if (!invalid && isTemplateSourcePath(name)) {
                ranges = parseTemplateModel(contents).FallbackRanges;
            }
            cached = IncludedFallbacks.emplace(file, std::move(ranges)).first;
        }
        if (cached->second.empty()) return false;
        unsigned line = sourceManager.getSpellingLineNumber(expansion);
        if (line == 0U) return false;
        for (const TemplateLineRange &range : cached->second) {
            if (range.contains(line - 1U)) return true;
        }
        return false;
    }

    // A local declared with a structurally constrained (or otherwise
    // fallback-less) template parameter type is dropped by clang, so every
    // later use is reported as undeclared. Those reports are fallout of the
    // template parameter, not real errors.
    bool isDerivedFromTemplateParameter(size_t line,
                                        llvm::StringRef message) const
    {
        llvm::StringRef rest = message;
        if (!rest.consume_front("use of undeclared identifier '")) {
            return false;
        }
        llvm::StringRef name = rest.take_until([](char c) {
            return c == '\'';
        });
        const TemplateDeclaration *declaration = Model->declarationAt(line);
        if (declaration == nullptr || name.empty() ||
            declaration->BodyCloseOffset >= Masked.size()) {
            return false;
        }
        std::optional<std::string> type = variableTypeInDeclaration(
            *declaration, Masked, name, declaration->BodyCloseOffset);
        return type && containsWord(typeTokens(*type),
                                    declaration->Parameters);
    }

    bool HasTemplateModel = false;
    std::optional<TemplateModel> Model;
    std::string Masked;
    std::set<unsigned> ClangDiagnosticOffsets;
    std::map<clang::FileID, std::vector<TemplateLineRange>> IncludedFallbacks;
};

class TmplgenFeatureModule final : public FeatureModule {
public:
    void initializeLSP(LSPBinder &Bind, const llvm::json::Object &,
                       llvm::json::Object &ServerCaps) override
    {
        KeywordTokenType = ensureKeywordTokenType(ServerCaps);
        Bind.method("textDocument/completion", this,
                    &TmplgenFeatureModule::onCompletion);
        Bind.method("textDocument/definition", this,
                    &TmplgenFeatureModule::onDefinition);
        Bind.method("textDocument/semanticTokens/full", this,
                    &TmplgenFeatureModule::onSemanticTokens);
        Bind.method("textDocument/semanticTokens/full/delta", this,
                    &TmplgenFeatureModule::onSemanticTokensDelta);
    }

    std::unique_ptr<ASTListener> astListeners() override
    {
        return std::make_unique<TmplgenASTListener>();
    }

private:
    void onCompletion(const CompletionParams &Params,
                      Callback<CompletionList> Reply)
    {
        llvm::StringRef Path = Params.textDocument.uri.file();
        bool Template = isTemplateSourcePath(Path);
        scheduler().runWithPreamble(
            "tmplgen completion", Path, TUScheduler::StaleOrAbsent,
            [this, Params, Template, File = Path.str(),
             Reply = std::move(Reply)](
                llvm::Expected<InputsAndPreamble> In) mutable {
                if (!In) return Reply(In.takeError());
                if (Template) {
                    // Structurally constrained member access needs the AST
                    // to resolve the schema rows; everything else uses the
                    // regular (preamble based) completion.
                    TemplateModel Model = parseTemplateModel(In->Contents);
                    if (structuralAccessAt(Model, In->Contents,
                                           Params.position)) {
                        return completeStructural(Params, File,
                                                  std::move(Reply));
                    }
                }
                ParseInputs Inputs{
                    In->Command, &fs(), In->Contents.str()};
                if (Inputs.Contents.empty() ||
                    Inputs.Contents.back() != '\n') {
                    Inputs.Contents.push_back('\n');
                }
                Inputs.Index = index();
                CodeCompleteOptions Options;
                Options.Index = index();
                Options.MainFileSignals = In->Signals;
                CodeCompleteResult Base = codeComplete(
                    File, Params.position, In->Preamble, Inputs, Options);
                Reply(renderCompletions(Base, Options));
            });
    }

    void completeStructural(const CompletionParams &Params,
                            const std::string &File,
                            Callback<CompletionList> Reply)
    {
        scheduler().runWithAST(
            "tmplgen structural completion", File,
            [Params, Reply = std::move(Reply)](
                llvm::Expected<InputsAndAST> In) mutable {
                if (!In) return Reply(In.takeError());
                llvm::StringRef Code = In->Inputs.Contents;
                TemplateModel Model = parseTemplateModel(Code);
                std::optional<StructuralAccessContext> Access =
                    structuralAccessAt(Model, Code, Params.position);
                std::optional<CompletionList> List;
                if (Access) {
                    List = structuralCompletions(In->AST, *Access, Code,
                                                 Params.position);
                }
                Reply(List ? std::move(*List) : CompletionList{});
            });
    }

    void onSemanticTokens(const SemanticTokensParams &Params,
                          Callback<SemanticTokens> Reply)
    {
        scheduler().runWithAST(
            "tmplgen semantic tokens", Params.textDocument.uri.file(),
            [this, Params, Reply = std::move(Reply)](
                llvm::Expected<InputsAndAST> In) mutable {
                if (!In) return Reply(In.takeError());
                std::vector<SemanticToken> Tokens = semanticTokensForAST(
                    In->AST, Params.textDocument.uri.file(),
                    In->Inputs.Contents, KeywordTokenType);
                SemanticTokens Result;
                Result.tokens = std::move(Tokens);
                {
                    std::lock_guard<std::mutex> Lock(SemanticMutex);
                    SemanticTokens &Last = LastSemanticTokens[
                        Params.textDocument.uri.file().str()];
                    Last.tokens = Result.tokens;
                    incrementResultId(Last.resultId);
                    Result.resultId = Last.resultId;
                }
                Reply(std::move(Result));
            });
    }

    void onSemanticTokensDelta(const SemanticTokensDeltaParams &Params,
                               Callback<SemanticTokensOrDelta> Reply)
    {
        scheduler().runWithAST(
            "tmplgen semantic tokens delta",
            Params.textDocument.uri.file(),
            [this, Params, Reply = std::move(Reply)](
                llvm::Expected<InputsAndAST> In) mutable {
                if (!In) return Reply(In.takeError());
                std::vector<SemanticToken> Tokens = semanticTokensForAST(
                    In->AST, Params.textDocument.uri.file(),
                    In->Inputs.Contents, KeywordTokenType);
                SemanticTokensOrDelta Result;
                {
                    std::lock_guard<std::mutex> Lock(SemanticMutex);
                    SemanticTokens &Last = LastSemanticTokens[
                        Params.textDocument.uri.file().str()];
                    if (Params.previousResultId == Last.resultId) {
                        Result.edits = diffTokens(Last.tokens, Tokens);
                    } else {
                        Result.tokens = Tokens;
                    }
                    Last.tokens = std::move(Tokens);
                    incrementResultId(Last.resultId);
                    Result.resultId = Last.resultId;
                }
                Reply(std::move(Result));
            });
    }

    void onDefinition(const TextDocumentPositionParams &Params,
                      Callback<std::vector<Location>> Reply)
    {
        scheduler().runWithAST(
            "tmplgen definition", Params.textDocument.uri.file(),
            [this, Params, Reply = std::move(Reply)](
                llvm::Expected<InputsAndAST> In) mutable {
                if (!In) return Reply(In.takeError());
                std::vector<Location> ConstraintDefinitions =
                    constraintDefinitionLocations(
                        In->AST, Params, Params.textDocument.uri.file());
                if (!ConstraintDefinitions.empty()) {
                    return Reply(std::move(ConstraintDefinitions));
                }
                std::vector<LocatedSymbol> Symbols =
                    locateSymbolAt(In->AST, Params.position, index());
                std::vector<Location> Definitions;
                for (const LocatedSymbol &Symbol : Symbols) {
                    Location Toggle;
                    if (isToggleTarget(Params, Symbol, Toggle)) {
                        return Reply(std::vector<Location>{std::move(Toggle)});
                    }
                    Location Definition =
                        Symbol.Definition.value_or(Symbol.PreferredDeclaration);
                    if (std::optional<Location> Template =
                            templateFunctionLocation(
                                Symbol, Definition,
                                Params.textDocument.uri.file())) {
                        Definition = std::move(*Template);
                    } else if (std::optional<Location> Type =
                                   fallbackTemplateTypeLocation(
                                       Symbol, Definition,
                                       Params.textDocument.uri.file())) {
                        Definition = std::move(*Type);
                    }
                    Definitions.push_back(std::move(Definition));
                }
                Reply(std::move(Definitions));
            });
    }

    unsigned KeywordTokenType =
        static_cast<unsigned>(HighlightingKind::LastKind) + 1U;
    std::mutex SemanticMutex;
    std::unordered_map<std::string, SemanticTokens> LastSemanticTokens;
};

FeatureModuleRegistry::Add<TmplgenFeatureModule> Registration(
    "tmplgen", "regenerate C template specializations while parsing files");

} // namespace
} // namespace clang::clangd
