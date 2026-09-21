#ifndef BZM_FRAME_PARSER_H
#define BZM_FRAME_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/protocol.h"

#define BZM_TDM_HEADER_SIZE 2U
#define BZM_TDM_BROADCAST_ASIC_ID BZM_BROADCAST_ASIC
#define BZM_TDM_TELEMETRY_PAYLOAD_SIZE 8U
#define BZM_TDM_NOOP_PAYLOAD_SIZE 3U
#define BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE 32U
#define BZM_TDM_MAX_FRAME_SIZE (BZM_TDM_HEADER_SIZE + BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE)

typedef enum
{
    BZM_FRAME_RESULT = 0x01,
    BZM_FRAME_REGISTER = 0x03,
    BZM_FRAME_TELEMETRY = 0x0d,
    BZM_FRAME_NOOP = 0x0f,
} bzm_frame_type_t;

typedef struct
{
    bzm_frame_type_t type;
    uint8_t asic_id;
    uint8_t payload[BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE];
    size_t payload_length;
    uint64_t timestamp_us;
} bzm_frame_t;

typedef void (*bzm_frame_handler_t)(void * context, const bzm_frame_t * frame);

typedef struct
{
    uint8_t buffer[BZM_TDM_MAX_FRAME_SIZE];
    size_t buffered_length;
    uint8_t expected_register_length[BZM_MAX_ASIC_COUNT];
    bzm_frame_handler_t handler;
    void * handler_context;
    uint32_t emitted_frames;
    uint32_t discarded_bytes;
} bzm_frame_parser_t;

void bzm_frame_parser_init(bzm_frame_parser_t * parser, bzm_frame_handler_t handler, void * handler_context);

/*
 * Register responses carry no payload length on the wire.  A reader must
 * register the requested length before sending the read command.  Only one
 * outstanding register response per ASIC is supported by this parser.
 * Register replies are accepted only from addressed ASIC IDs 0x0a, 0x14,
 * 0x1e, and 0x28;
 * the broadcast ID is accepted only for fixed-length frame types such as the
 * discovery NOOP response.
 */
bool bzm_frame_parser_expect_register(bzm_frame_parser_t * parser, uint8_t asic_id, size_t payload_length);
bool bzm_frame_parser_cancel_register(bzm_frame_parser_t * parser, uint8_t asic_id);

size_t bzm_frame_parser_feed(bzm_frame_parser_t * parser, const uint8_t * data, size_t data_length, uint64_t timestamp_us);
size_t bzm_frame_parser_pending_bytes(const bzm_frame_parser_t * parser);

#endif // BZM_FRAME_PARSER_H
