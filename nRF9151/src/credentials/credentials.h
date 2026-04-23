/*
 * Copyright (c) 2020 Circuit Dojo LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define CRED_SERVER_MAX_LEN      64
#define CRED_AUTH_TOKEN_MAX_LEN  64
#define CRED_TEMPLATE_MAX_LEN    32

/**
 * @brief Initialise credentials. Reads from NVS; if auth token is missing,
 *        prompts over UART and stores to NVS before returning.
 *
 * @return 0 on success, negative errno otherwise.
 */
int credentials_init(void);

const char *credentials_get_server(void);
const char *credentials_get_auth_token(void);
const char *credentials_get_template_id(void);

#endif /* CREDENTIALS_H */
