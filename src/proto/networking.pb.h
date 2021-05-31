/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.5 */

#ifndef PB_NETWORKING_PB_H_INCLUDED
#define PB_NETWORKING_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Struct definitions */
typedef struct _MsgSocketBindRequest { 
    uint32_t address; 
    int32_t port; 
} MsgSocketBindRequest;

typedef struct _MsgSocketBindResponse { 
    int32_t err; 
} MsgSocketBindResponse;

typedef struct _MsgSocketOpenRequest { 
    uint32_t type; 
    uint32_t protocol; 
} MsgSocketOpenRequest;

typedef struct _MsgSocketOpenResponse { 
    int32_t handle; 
    int32_t err; 
} MsgSocketOpenResponse;

typedef struct _MsgRequest { 
    uint32_t id; 
    pb_size_t which_payload;
    union {
        MsgSocketOpenRequest socketOpenRequest;
    } payload; 
} MsgRequest;

typedef struct _MsgResponse { 
    uint32_t id; 
    pb_size_t which_payload;
    union {
        MsgSocketOpenResponse socketOpenResponse;
    } payload; 
} MsgResponse;


#ifdef __cplusplus
extern "C" {
#endif

/* Initializer values for message structs */
#define MsgSocketOpenRequest_init_default        {0, 0}
#define MsgSocketOpenResponse_init_default       {0, 0}
#define MsgSocketBindRequest_init_default        {0, 0}
#define MsgSocketBindResponse_init_default       {0}
#define MsgRequest_init_default                  {0, 0, {MsgSocketOpenRequest_init_default}}
#define MsgResponse_init_default                 {0, 0, {MsgSocketOpenResponse_init_default}}
#define MsgSocketOpenRequest_init_zero           {0, 0}
#define MsgSocketOpenResponse_init_zero          {0, 0}
#define MsgSocketBindRequest_init_zero           {0, 0}
#define MsgSocketBindResponse_init_zero          {0}
#define MsgRequest_init_zero                     {0, 0, {MsgSocketOpenRequest_init_zero}}
#define MsgResponse_init_zero                    {0, 0, {MsgSocketOpenResponse_init_zero}}

/* Field tags (for use in manual encoding/decoding) */
#define MsgSocketBindRequest_address_tag         1
#define MsgSocketBindRequest_port_tag            2
#define MsgSocketBindResponse_err_tag            1
#define MsgSocketOpenRequest_type_tag            1
#define MsgSocketOpenRequest_protocol_tag        2
#define MsgSocketOpenResponse_handle_tag         1
#define MsgSocketOpenResponse_err_tag            2
#define MsgRequest_id_tag                        1
#define MsgRequest_socketOpenRequest_tag         2
#define MsgResponse_id_tag                       1
#define MsgResponse_socketOpenResponse_tag       2

/* Struct field encoding specification for nanopb */
#define MsgSocketOpenRequest_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   type,              1) \
X(a, STATIC,   REQUIRED, UINT32,   protocol,          2)
#define MsgSocketOpenRequest_CALLBACK NULL
#define MsgSocketOpenRequest_DEFAULT NULL

#define MsgSocketOpenResponse_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, INT32,    handle,            1) \
X(a, STATIC,   REQUIRED, INT32,    err,               2)
#define MsgSocketOpenResponse_CALLBACK NULL
#define MsgSocketOpenResponse_DEFAULT NULL

#define MsgSocketBindRequest_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   address,           1) \
X(a, STATIC,   REQUIRED, INT32,    port,              2)
#define MsgSocketBindRequest_CALLBACK NULL
#define MsgSocketBindRequest_DEFAULT NULL

#define MsgSocketBindResponse_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, INT32,    err,               1)
#define MsgSocketBindResponse_CALLBACK NULL
#define MsgSocketBindResponse_DEFAULT NULL

#define MsgRequest_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   id,                1) \
X(a, STATIC,   ONEOF,    MESSAGE,  (payload,socketOpenRequest,payload.socketOpenRequest),   2)
#define MsgRequest_CALLBACK NULL
#define MsgRequest_DEFAULT NULL
#define MsgRequest_payload_socketOpenRequest_MSGTYPE MsgSocketOpenRequest

#define MsgResponse_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   id,                1) \
X(a, STATIC,   ONEOF,    MESSAGE,  (payload,socketOpenResponse,payload.socketOpenResponse),   2)
#define MsgResponse_CALLBACK NULL
#define MsgResponse_DEFAULT NULL
#define MsgResponse_payload_socketOpenResponse_MSGTYPE MsgSocketOpenResponse

extern const pb_msgdesc_t MsgSocketOpenRequest_msg;
extern const pb_msgdesc_t MsgSocketOpenResponse_msg;
extern const pb_msgdesc_t MsgSocketBindRequest_msg;
extern const pb_msgdesc_t MsgSocketBindResponse_msg;
extern const pb_msgdesc_t MsgRequest_msg;
extern const pb_msgdesc_t MsgResponse_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define MsgSocketOpenRequest_fields &MsgSocketOpenRequest_msg
#define MsgSocketOpenResponse_fields &MsgSocketOpenResponse_msg
#define MsgSocketBindRequest_fields &MsgSocketBindRequest_msg
#define MsgSocketBindResponse_fields &MsgSocketBindResponse_msg
#define MsgRequest_fields &MsgRequest_msg
#define MsgResponse_fields &MsgResponse_msg

/* Maximum encoded size of messages (where known) */
#define MsgRequest_size                          20
#define MsgResponse_size                         30
#define MsgSocketBindRequest_size                17
#define MsgSocketBindResponse_size               11
#define MsgSocketOpenRequest_size                12
#define MsgSocketOpenResponse_size               22

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif