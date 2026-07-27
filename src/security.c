#include "security.h"
#include <string.h>
#include <ctype.h>

/* Symbol rule: 1-7 uppercase/lowercase letters or digits, non-empty,
 * fits in SYMBOL_MAX_LEN including NUL. */
bool secValidateSymbol(const char *symbol)
{
    bool valid = true;
    size_t len;

    if (symbol == NULL)
    {
        valid = false;
    }
    else
    {
        len = portable_strnlen(symbol, SYMBOL_MAX_LEN + 1U);
        if ((len == 0U) || (len >= SYMBOL_MAX_LEN))
        {
            valid = false;
        }
        else
        {
            size_t i;
            for (i = 0U; i < len; i++)
            {
                if (isalnum((unsigned char)symbol[i]) == 0)
                {
                    valid = false;
                    break;
                }
            }
        }
    }
    return valid;
}

bool secValidatePrice(double price)
{
    bool valid = true;
    if ((price < MIN_PRICE) || (price > MAX_PRICE))
    {
        valid = false;
    }
    return valid;
}

bool secValidateUsername(const char *username)
{
    bool valid = true;
    size_t len;

    if (username == NULL)
    {
        valid = false;
    }
    else
    {
        len = portable_strnlen(username, USERNAME_MAX_LEN + 1U);
        if ((len == 0U) || (len >= USERNAME_MAX_LEN))
        {
            valid = false;
        }
        else
        {
            size_t i;
            for (i = 0U; i < len; i++)
            {
                char c = username[i];
                if ((isalnum((unsigned char)c) == 0) && (c != '_') && (c != '.'))
                {
                    valid = false;
                    break;
                }
            }
        }
    }
    return valid;
}

bool secValidatePassword(const char *password)
{
    bool valid = true;
    size_t len;
    static const size_t MIN_PASSWORD_LEN = 4U;

    if (password == NULL)
    {
        valid = false;
    }
    else
    {
        len = portable_strnlen(password, PASSWORD_MAX_LEN + 1U);
        if ((len < MIN_PASSWORD_LEN) || (len >= PASSWORD_MAX_LEN))
        {
            valid = false;
        }
        else
        {
            size_t i;
            for (i = 0U; i < len; i++)
            {
                if (isspace((unsigned char)password[i]) != 0)
                {
                    valid = false;
                    break;
                }
            }
        }
    }
    return valid;
}

bool secValidateMenuChoice(int choice, int minChoice, int maxChoice)
{
    bool valid = true;
    if ((choice < minChoice) || (choice > maxChoice))
    {
        valid = false;
    }
    return valid;
}

bool secValidateCompanyName(const char *name)
{
    bool valid = true;
    size_t len;

    if (name == NULL)
    {
        valid = false;
    }
    else
    {
        len = portable_strnlen(name, NAME_MAX_LEN + 1U);
        if ((len == 0U) || (len >= NAME_MAX_LEN))
        {
            valid = false;
        }
    }
    return valid;
}
