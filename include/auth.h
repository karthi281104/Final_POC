#ifndef AUTH_H
#define AUTH_H

#include "common.h"
#include <stdbool.h>

typedef struct {
    User users[MAX_USERS];
    size_t count;
} UserStore;

status_t authInit(UserStore *store);

/* Loads users.txt (format: username|password|role). If the file does
 * not exist, creates it with default admin/user accounts. */
status_t authLoadUsersFromPath(UserStore *store, const char *path);
status_t authLoadUsers(UserStore *store);
status_t authSaveUsersToPath(UserStore *store, const char *path);
status_t authSaveUsers(UserStore *store);

status_t authCreateDefaultUsers(const char *path);

/* Returns STATUS_OK and fills outUser on success, STATUS_ERR_AUTH otherwise. */
status_t authLogin(UserStore *store, const char *username, const char *password, User *outUser);

bool authIsAdmin(const User *user);
bool authIsTester(const User *user);

#endif /* AUTH_H */
