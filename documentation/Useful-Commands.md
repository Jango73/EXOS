# Useful Commands

## Perl

Remove DEBUG(...) calls, including multi-line invocations:

```bash
perl -0pi -e 's/^[ \t]*DEBUG\([^;]*?\);\n//smg' some-file.c
```
