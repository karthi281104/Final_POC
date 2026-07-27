#ifndef SECURITY_H
#define SECURITY_H

#include "common.h"
#include <stdbool.h>

/* Each returns true if the input is valid per the rules documented in
 * security.c. All are pure/side-effect-free so the tester module can
 * exercise them repeatedly. */
bool secValidateSymbol(const char *symbol);
bool secValidatePrice(double price);
bool secValidateUsername(const char *username);
bool secValidatePassword(const char *password);
bool secValidateMenuChoice(int choice, int minChoice, int maxChoice);
bool secValidateCompanyName(const char *name);

#endif /* SECURITY_H */
