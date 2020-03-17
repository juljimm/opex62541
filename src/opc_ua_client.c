
#include <open62541.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include "erlcmd.h"

static const char response_id = 'r';

UA_Client *client;

/*****************************/
/* Elixir Message assemblers */
/*****************************/
// https://open62541.org/doc/current/statuscodes.html?highlight=error
static void send_opex_response(uint32_t reason)
{
    char resp[256];
    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "error");
    ei_encode_ulong(resp, &resp_index, reason);
    erlcmd_send(resp, resp_index);
}


static void encode_client_config(char *resp, int *resp_index, void *data)
{
    ei_encode_map_header(resp, resp_index, 3);
    ei_encode_atom(resp, resp_index, "timeout");
    ei_encode_long(resp, resp_index,((UA_ClientConfig *)data)->timeout);
    
    ei_encode_atom(resp, resp_index, "secureChannelLifeTime");
    ei_encode_long(resp, resp_index,((UA_ClientConfig *)data)->secureChannelLifeTime);
    
    ei_encode_atom(resp, resp_index, "requestedSessionTimeout");
    ei_encode_long(resp, resp_index,((UA_ClientConfig *)data)->requestedSessionTimeout);
}

/**
 * @brief Send :ok back to Elixir
 */
static void send_ok_response()
{
    char resp[256];
    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
    ei_encode_version(resp, &resp_index);
    ei_encode_atom(resp, &resp_index, "ok");
    erlcmd_send(resp, resp_index);
}

/**
 * @brief Send data back to Elixir in form of {:ok, data}
 */
static void send_data_response(void *data, int data_type, int data_len)
{
    char resp[256];
    char version[5];
    char r_len = 1;
    long i_struct;
    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "ok");

    switch(data_type)
    {
        case 1: //signed (long)
            ei_encode_long(resp, &resp_index,*(int32_t *)data);
        break;

        case 2: //unsigned (long)
            ei_encode_ulong(resp, &resp_index,*(uint32_t *)data);
        break;

        case 3: //strings
            ei_encode_string(resp, &resp_index, data);
        break;

        case 4: //doubles
            ei_encode_double(resp, &resp_index, *(double *)data );
        break;

        case 5: //arrays (byte type)
            ei_encode_binary(resp, &resp_index, data, data_len);
        break;

        case 6: //atom
            ei_encode_atom(resp, &resp_index, data);
        break;

        case 7: //UA_ClientConfig
            encode_client_config(resp, &resp_index, data);
        break;

        default:
            errx(EXIT_FAILURE, "data_type error");
        break;
    }

    erlcmd_send(resp, resp_index);
}

/**
 * @brief Send a response of the form {:error, reason}
 *
 * @param reason is an error reason (sended back as an atom)
 */
static void send_error_response(const char *reason)
{
    char resp[256];
    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "error");
    ei_encode_atom(resp, &resp_index, reason);
    erlcmd_send(resp, resp_index);
}

static void handle_test(const char *req, int *req_index)
{
    send_ok_response();    
}

/***************************************/
/* Configuration & Lifecycle Functions */
/***************************************/

/**
 *  This is function allows to configure the client. 
*/
static void handle_set_client_config(const char *req, int *req_index)
{
    int i_key;
    int term_size;
    unsigned long value;

    UA_ClientConfig *config = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(config);

    if(ei_decode_map_header(req, req_index, &term_size) < 0)
    errx(EXIT_FAILURE, ":set_client_config inconsistent argument arity = %d", term_size);    
    for(i_key = 0; i_key < term_size; i_key++)
    {
        char atom[30];
        if (ei_decode_atom(req, req_index, atom) < 0) {
            send_error_response("einval");
            return;
        }
        
        if (ei_decode_ulong(req, req_index, &value) < 0) {
            send_error_response("einval_2");
            return;
        }

        if(!strcmp(atom, "timeout")) 
            config->timeout = (int)value;
        else if(!strcmp(atom, "requestedSessionTimeout")) 
            config->requestedSessionTimeout = (int)value;
        else if(!strcmp(atom, "secureChannelLifeTime")) 
            config->secureChannelLifeTime = (int)value;
        else
        {
            send_error_response("einval");
            return;
        }
    }

    send_ok_response();
}

/* 
*   Get the client configuration. 
*/
static void handle_get_client_config(const char *req, int *req_index)
{
    UA_ClientConfig *config = UA_Client_getConfig(client);
    send_data_response(config, 7, 0);
}

/*
 * Gets the current client connection state. 
*/
static void handle_get_client_state(const char *req, int *req_index)
{
    UA_ClientState state = UA_Client_getState(client);

    switch(state)
    {
        case UA_CLIENTSTATE_DISCONNECTED:
            send_data_response("Disconnected", 3, 0);
        break;

        case UA_CLIENTSTATE_WAITING_FOR_ACK:
            send_data_response("Wating for ACK", 3, 0);
        break;

        case UA_CLIENTSTATE_CONNECTED:
            send_data_response("Connected", 3, 0);
        break;

        case UA_CLIENTSTATE_SECURECHANNEL:
            send_data_response("Secure Channel", 3, 0);
        break;

        case UA_CLIENTSTATE_SESSION:
            send_data_response("Session", 3, 0);
        break;

        case UA_CLIENTSTATE_SESSION_DISCONNECTED:
            send_data_response("Session disconnected", 3, 0);
        break;

        case UA_CLIENTSTATE_SESSION_RENEWED:
            send_data_response("session renewed", 3, 0);
        break;
    }
}

/* 
*   Resets a client. 
*/
static void handle_reset_client(const char *req, int *req_index)
{
    UA_Client_reset(client);
    send_ok_response();
}

/************************/
/* Connection Functions */
/************************/

/* Connect to the server by passing only the url.
 *
 * @return Indicates whether the operation succeeded or returns an error code */
static void handle_connect_client_by_url(const char *req, int *req_index)
{
    int term_size;
    int term_type;

    if(ei_decode_tuple_header(req, req_index, &term_size) < 0 ||
        term_size != 2)
        errx(EXIT_FAILURE, ":connect_client_by_url requires a 2-tuple, term_size = %d", term_size);

    unsigned long str_len;
    if (ei_decode_ulong(req, req_index, &str_len) < 0) {
        send_error_response("einval");
        return;
    }

    char url[str_len + 1];
    long binary_len;
    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(url) ||
            ei_decode_binary(req, req_index, url, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist.
        send_error_response("enoent");
        return;
    }
    url[binary_len] = '\0';

    UA_StatusCode retval = UA_Client_connect(client, url);
    if(retval != UA_STATUSCODE_GOOD) {
        send_opex_response(retval);
        return;
    }
    
    send_ok_response();
}

/* Connect to the server by passing a url, username and password.
 *
 * @return Indicates whether the operation succeeded or returns an error code */
static void handle_connect_client_by_username(const char *req, int *req_index)
{
    int term_size;
    int term_type;
    unsigned long str_len;
    unsigned long binary_len;

    if(ei_decode_tuple_header(req, req_index, &term_size) < 0 || term_size != 6)
        errx(EXIT_FAILURE, ":connect_client_by_username requires a 6-tuple, term_size = %d", term_size);
    
    
    // url_size
    if (ei_decode_ulong(req, req_index, &str_len) < 0) {
        send_error_response("einval");
        return;
    }

    // url
    char url[str_len + 1];
    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(url) ||
            ei_decode_binary(req, req_index, url, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist.
        send_error_response("enoent");
        return;
    }
    url[binary_len] = '\0';

    // username_size
    if (ei_decode_ulong(req, req_index, &str_len) < 0) {
        send_error_response("einval");
        return;
    }

    // username
    char username[str_len + 1];
    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(username) ||
            ei_decode_binary(req, req_index, username, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist.
        send_error_response("enoent");
        return;
    }
    username[binary_len] = '\0';

    // password_size
    if (ei_decode_ulong(req, req_index, &str_len) < 0) {
        send_error_response("einval");
        return;
    }

    // password
    char password[str_len + 1];
    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(password) ||
            ei_decode_binary(req, req_index, password, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist.
        send_error_response("enoent");
        return;
    }
    password[binary_len] = '\0';

    UA_StatusCode retval = UA_Client_connect_username(client, url, username, password);
    if(retval != UA_STATUSCODE_GOOD) {
        send_opex_response(retval);
        return;
    }
    
    send_ok_response();
}

/* Connect to the server without creating a session.
 *
 * @return Indicates whether the operation succeeded or returns an error code */
static void handle_connect_client_no_session(const char *req, int *req_index)
{
    int term_size;
    int term_type;

    if(ei_decode_tuple_header(req, req_index, &term_size) < 0 ||
        term_size != 2)
        errx(EXIT_FAILURE, ":connect_client_no_session requires a 2-tuple, term_size = %d", term_size);

    unsigned long str_len;
    if (ei_decode_ulong(req, req_index, &str_len) < 0) {
        send_error_response("einval");
        return;
    }

    char url[str_len + 1];
    long binary_len;
    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(url) ||
            ei_decode_binary(req, req_index, url, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist.
        send_error_response("enoent");
        return;
    }
    url[binary_len] = '\0';
    
    UA_StatusCode retval = UA_Client_connect_noSession(client, url);
    if(retval != UA_STATUSCODE_GOOD) {
        send_opex_response(retval);
        return;
    }
    
    send_ok_response();
}

/* Disconnect and close a connection to the selected server.
 *
 * @return Indicates whether the operation succeeded or returns an error code */
static void handle_disconnect_client(const char *req, int *req_index)
{
    
    UA_StatusCode retval = UA_Client_disconnect(client);
    if(retval != UA_STATUSCODE_GOOD) {
        send_opex_response(retval);
        return;
    }
    
    send_ok_response();
}

/*******************************/
/* Elixir -> C Message Handler */
/*******************************/

struct request_handler {
    const char *name;
    void (*handler)(const char *req, int *req_index);
};

/*  Elixir request handler table
 *  FIXME: Order roughly based on most frequent calls to least (WIP).
 */
static struct request_handler request_handlers[] = {
    {"test", handle_test},
    // lifecycle functions
    {"get_client_state", handle_get_client_state},     
    {"set_client_config", handle_set_client_config},     
    {"get_client_config", handle_get_client_config},     
    {"reset_client", handle_reset_client},
    // connections functions
    {"connect_client_by_url", handle_connect_client_by_url},
    {"connect_client_by_username", handle_connect_client_by_username},     
    {"connect_client_no_session", handle_connect_client_no_session},     
    {"disconnect_client", handle_disconnect_client},     
    { NULL, NULL }
};

/**
 * @brief Decode and forward requests from Elixir to the appropriate handlers
 * @param req the undecoded request
 * @param cookie
 */
static void handle_elixir_request(const char *req, void *cookie)
{
    (void) cookie;

    // Commands are of the form {Command, Arguments}:
    // { atom(), term() }
    int req_index = sizeof(uint16_t);
    if (ei_decode_version(req, &req_index, NULL) < 0)
        errx(EXIT_FAILURE, "Message version issue?");

    int arity;
    if (ei_decode_tuple_header(req, &req_index, &arity) < 0 ||
            arity != 2)
        errx(EXIT_FAILURE, "expecting {cmd, args} tuple");

    char cmd[MAXATOMLEN];
    if (ei_decode_atom(req, &req_index, cmd) < 0)
        errx(EXIT_FAILURE, "expecting command atom");
    
    //execute all handler
    for (struct request_handler *rh = request_handlers; rh->name != NULL; rh++) {
        if (strcmp(cmd, rh->name) == 0) {
            rh->handler(req, &req_index);
            return;
        }
    }
    // no listed function
    errx(EXIT_FAILURE, "unknown command: %s", cmd);
}

int main()
{
    client = UA_Client_new();

    struct erlcmd *handler = malloc(sizeof(struct erlcmd));
    erlcmd_init(handler, handle_elixir_request, NULL);

    for (;;) {
        struct pollfd fdset;

        fdset.fd = STDIN_FILENO;
        fdset.events = POLLIN;
        fdset.revents = 0;

        int timeout = -1; // Wait forever unless told by otherwise
        int rc = poll(&fdset, 1, timeout);

        if (rc < 0) {
            // Retry if EINTR
            if (errno == EINTR)
                continue;

            err(EXIT_FAILURE, "poll");
        }

        if (fdset.revents & (POLLIN | POLLHUP)) {
            if (erlcmd_process(handler))
                break;
        }
    }
    
    /* Disconnects the client internally */
    UA_Client_delete(client); 
}