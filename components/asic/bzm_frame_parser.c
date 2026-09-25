#include "bzm/frame_parser.h"

#include <string.h>

static bool asic_index(uint8_t asic_id, size_t * index)
{
    return bzm_topology_asic_index(asic_id, index);
}

static bool valid_wire_asic_id(uint8_t asic_id)
{
    return asic_index(asic_id, NULL) || asic_id == BZM_TDM_BROADCAST_ASIC_ID;
}

void bzm_frame_parser_init(bzm_frame_parser_t * parser, bzm_frame_handler_t handler, void * handler_context)
{
    if (parser == NULL)
        return;
    *parser = (bzm_frame_parser_t){
        .handler = handler,
        .handler_context = handler_context,
    };
}

bool bzm_frame_parser_expect_register(bzm_frame_parser_t * parser, uint8_t asic_id, size_t payload_length)
{
    size_t index;
    if (parser == NULL || !asic_index(asic_id, &index) || payload_length == 0 ||
        payload_length > BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE || parser->expected_register_length[index] != 0) {
        return false;
    }
    parser->expected_register_length[index] = payload_length;
    return true;
}

bool bzm_frame_parser_cancel_register(bzm_frame_parser_t * parser, uint8_t asic_id)
{
    size_t index;
    if (parser == NULL || !asic_index(asic_id, &index) || parser->expected_register_length[index] == 0) {
        return false;
    }
    parser->expected_register_length[index] = 0;
    return true;
}

static void discard_prefix(bzm_frame_parser_t * parser, size_t count)
{
    if (count < parser->buffered_length) {
        memmove(parser->buffer, parser->buffer + count, parser->buffered_length - count);
    }
    parser->buffered_length -= count;
}

static size_t expected_frame_length(bzm_frame_parser_t * parser, bool * valid_header)
{
    *valid_header = false;
    if (parser->buffered_length < BZM_TDM_HEADER_SIZE)
        return 0;

    size_t index = 0;
    bool addressed_asic = asic_index(parser->buffer[0], &index);
    if (!valid_wire_asic_id(parser->buffer[0]))
        return 0;

    size_t payload_length;
    switch (parser->buffer[1]) {
    case BZM_FRAME_RESULT:
        payload_length = BZM_RESULT_FRAME_SIZE;
        break;
    case BZM_FRAME_REGISTER:
        if (!addressed_asic) {
            return 0;
        }
        payload_length = parser->expected_register_length[index];
        if (payload_length == 0) {
            return 0;
        }
        break;
    case BZM_FRAME_TELEMETRY:
        payload_length = BZM_TDM_TELEMETRY_PAYLOAD_SIZE;
        break;
    case BZM_FRAME_NOOP:
        payload_length = BZM_TDM_NOOP_PAYLOAD_SIZE;
        break;
    default:
        return 0;
    }
    *valid_header = true;
    return BZM_TDM_HEADER_SIZE + payload_length;
}

static size_t parse_buffer(bzm_frame_parser_t * parser, uint64_t timestamp_us)
{
    size_t emitted = 0;
    while (parser->buffered_length >= BZM_TDM_HEADER_SIZE) {
        bool valid_header;
        size_t frame_length = expected_frame_length(parser, &valid_header);
        if (!valid_header) {
            discard_prefix(parser, 1);
            parser->discarded_bytes++;
            continue;
        }
        if (parser->buffered_length < frame_length)
            break;

        bzm_frame_t frame = {
            .type = (bzm_frame_type_t) parser->buffer[1],
            .asic_id = parser->buffer[0],
            .payload_length = frame_length - BZM_TDM_HEADER_SIZE,
            .timestamp_us = timestamp_us,
        };
        memcpy(frame.payload, parser->buffer + BZM_TDM_HEADER_SIZE, frame.payload_length);
        discard_prefix(parser, frame_length);

        if (frame.type == BZM_FRAME_REGISTER) {
            size_t index = 0;
            if (!bzm_topology_asic_index(frame.asic_id, &index)) {
                continue;
            }
            parser->expected_register_length[index] = 0;
        }
        parser->emitted_frames++;
        emitted++;
        if (parser->handler != NULL) {
            parser->handler(parser->handler_context, &frame);
        }
    }
    return emitted;
}

size_t bzm_frame_parser_feed(bzm_frame_parser_t * parser, const uint8_t * data, size_t data_length, uint64_t timestamp_us)
{
    if (parser == NULL || (data == NULL && data_length != 0))
        return 0;

    size_t emitted = 0;
    for (size_t i = 0; i < data_length; ++i) {
        if (parser->buffered_length == sizeof(parser->buffer)) {
            discard_prefix(parser, 1);
            parser->discarded_bytes++;
        }
        parser->buffer[parser->buffered_length++] = data[i];
        emitted += parse_buffer(parser, timestamp_us);
    }
    return emitted;
}

size_t bzm_frame_parser_pending_bytes(const bzm_frame_parser_t * parser)
{
    return parser == NULL ? 0 : parser->buffered_length;
}
