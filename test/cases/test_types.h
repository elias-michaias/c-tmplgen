#ifndef TEST_TYPES_H
#define TEST_TYPES_H

typedef int TestInt;

typedef enum { ErrorNone, ErrorFailed } ErrorCode;

typedef unsigned char Tag;

typedef long InferredLong;
typedef double InferredDouble;
typedef enum { InferredErrorNone, InferredErrorFailed } InferredError;

typedef struct {
  int snacks;
} Dog;

#endif /* TEST_TYPES_H */
