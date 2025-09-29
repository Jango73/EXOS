/* Kernel stub for stdlib.h */
#ifndef STDLIB_H
#define STDLIB_H

typedef unsigned long size_t;
#define NULL ((void*)0)

/* Memory allocation functions - these need kernel-specific implementations */
/* For now, declare but don't implement - bcrypt will need adaptation */
void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);

#endif /* STDLIB_H */