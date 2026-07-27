#ifndef TESTING_H
#define TESTING_H

#include "common.h"

/* In-app Tester module (reachable from the Admin/Tester menu). Every
 * function prints its results directly to stdout. Everything the
 * project needs for verification lives here, in one executable - no
 * separate folder, no separate compiles, no shell scripts. */

int testerRunUnitTests(void);
int testerRunIntegrationTests(void);
int testerRunMemoryLeakTest(void);
int testerRunSecurityValidationTests(void);
int testerRunSmartTesting(void);

/* Runtime benchmarks / demos: these don't have a pass/fail verdict -
 * they print measured numbers so you can see WHY an algorithm was
 * chosen, not just take it on faith. */
void testerRunBenchmarkHashVsLinear(void);
void testerRunBenchmarkRwlockVsMutex(void);
void testerRunDemoLruVsFifo(void);

#endif /* TESTING_H */
