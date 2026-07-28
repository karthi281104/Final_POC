#include "auth.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

status_t authInit(UserStore *store)
{
    status_t result = STATUS_OK;
    if (store == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        store->count = 0U;
    }
    return result;
}

status_t authCreateDefaultUsers(const char *path)
{
    status_t result = STATUS_OK;
    if (path == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "w");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            (void)fprintf(fp, "admin|admin123|ADMIN\n");
            (void)fprintf(fp, "tester|tester123|TESTER\n");
            (void)fprintf(fp, "user|user123|USER\n");
            (void)fclose(fp);
        }
    }
    return result;
}

status_t authLoadUsersFromPath(UserStore *store, const char *path)
{
    status_t result = STATUS_OK;

    if ((store == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "r");
        if (fp == NULL)
        {
            /* File missing: create sane defaults and try once more. */
            result = authCreateDefaultUsers(path);
            if (result == STATUS_OK)
            {
                fp = fopen(path, "r");
            }
        }

        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            char line[LINE_MAX_LEN];
            store->count = 0U;
            while ((fgets(line, (int)sizeof(line), fp) != NULL) && (store->count < MAX_USERS))
            {
                char *userTok, *passTok, *roleTok;
                size_t len = strlen(line);
                while ((len > 0U) && ((line[len-1U] == '\n') || (line[len-1U] == '\r')))
                {
                    line[len-1U] = '\0';
                    len--;
                }
                if (line[0] == '\0')
                {
                    continue;
                }
                userTok = strtok(line, "|");
                passTok = strtok(NULL, "|");
                roleTok = strtok(NULL, "|");

                if ((userTok != NULL) && (passTok != NULL))
                {
                    User *u = &store->users[store->count];
                    (void)safe_strcpy(u->username, sizeof(u->username), userTok);
                    (void)safe_strcpy(u->password, sizeof(u->password), passTok);
                    if ((roleTok != NULL) && (strcmp(roleTok, "ADMIN") == 0))
                    {
                        u->role = ROLE_ADMIN;
                    }
                    else if ((roleTok != NULL) && (strcmp(roleTok, "TESTER") == 0))
                    {
                        u->role = ROLE_TESTER;
                    }
                    else
                    {
                        u->role = ROLE_USER;
                    }
                    store->count++;
                }
            }
            (void)fclose(fp);
        }
    }
    return result;
}

status_t authLoadUsers(UserStore *store)
{
    return authLoadUsersFromPath(store, DEFAULT_USERS_PATH);
}

static const char *roleToString(role_t role)
{
    const char *str;
    switch (role)
    {
        case ROLE_ADMIN:  str = "ADMIN";  break;
        case ROLE_TESTER: str = "TESTER"; break;
        default:          str = "USER";   break;
    }
    return str;
}

status_t authLogin(UserStore *store, const char *username, const char *password, User *outUser)
{
    status_t result = STATUS_ERR_AUTH;

    if ((store == NULL) || (username == NULL) || (password == NULL) || (outUser == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        size_t i;
        for (i = 0U; i < store->count; i++)
        {
            if ((strcmp(store->users[i].username, username) == 0) &&
                (strcmp(store->users[i].password, password) == 0))
            {
                *outUser = store->users[i];
                result = STATUS_OK;
                break;
            }
        }

        if (result == STATUS_OK)
        {
            (void)loggerLog(LOG_AUDIT, "LOGIN SUCCESS user=%s role=%s", username,
                             roleToString(outUser->role));
        }
        else
        {
            (void)loggerLog(LOG_AUDIT, "LOGIN FAILED user=%s", username);
        }
    }
    return result;
}

bool authIsAdmin(const User *user)
{
    bool isAdmin = false;
    if (user != NULL)
    {
        isAdmin = (user->role == ROLE_ADMIN);
    }
    return isAdmin;
}

bool authIsTester(const User *user)
{
    bool isTester = false;
    if (user != NULL)
    {
        isTester = (user->role == ROLE_TESTER);
    }
    return isTester;
}
