#ifndef PQC_WIRE_H
#define PQC_WIRE_H

/**
 * @file wire.h
 * @brief Application messages sent inside the TLS stream (RECORD and ACK).
 *
 * Layout and limits are defined in docs/telemetry_format.md sections 3 and 4.
 * All multi-byte fields are big-endian. Each message is
 * `type (1) | length (2) | payload (length)`.
 *
 * This module only builds and checks bytes. It does no I/O and no TLS, so it
 * can be tested without a network.
 */

#include <stddef.h>
#include <stdint.h>

/** @brief Message type byte for a telemetry RECORD. */
#define WIRE_TYPE_RECORD 0x01
/** @brief Message type byte for an ACK. */
#define WIRE_TYPE_ACK 0x02
/** @brief Bytes in the message header: type (1) and length (2). */
#define WIRE_HEADER_LEN 3
/** @brief Payload bytes in a RECORD. */
#define WIRE_RECORD_LEN 33
/** @brief Payload bytes in an ACK. */
#define WIRE_ACK_LEN 9
/** @brief Largest payload any receiver accepts (proposed in the spec). */
#define WIRE_MAX_PAYLOAD 64
/** @brief Bytes in a complete RECORD message, header included. */
#define WIRE_RECORD_MSG_LEN (WIRE_HEADER_LEN + WIRE_RECORD_LEN)
/** @brief Bytes in a complete ACK message, header included. */
#define WIRE_ACK_MSG_LEN (WIRE_HEADER_LEN + WIRE_ACK_LEN)

/** @brief One telemetry record (docs/telemetry_format.md section 3). */
struct wire_record {
  uint8_t node;     /**< sender ID; 1 = rover A */
  uint64_t seq;     /**< per-node sequence number, starts at 1, never 0 */
  uint64_t ts_ms;   /**< Pi A wall clock, ms since the Unix epoch */
  uint32_t pico_ms; /**< ms since Pico A booted (0 until a Pico exists) */
  int16_t imu[6];   /**< ax ay az gx gy gz, signed raw LSB */
};

/** @brief An acknowledgment: "this node+seq is committed". */
struct wire_ack {
  uint8_t node; /**< node of the acknowledged record */
  uint64_t seq; /**< seq of the acknowledged record */
};

/**
 * @brief Write a complete RECORD message (header and payload).
 * @param[in]  rec      Record to encode.
 * @param[out] out      Destination buffer.
 * @param[in]  out_cap  Size of @p out in bytes.
 * @param[out] out_len  Bytes written (WIRE_RECORD_MSG_LEN); 0 on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_BUFFER_TOO_SMALL if @p out_cap is too small,
 *         PQC_ERR_WIRE_BAD_FIELD if @p rec has seq 0.
 */
int wire_encode_record(const struct wire_record *rec, uint8_t *out,
                       size_t out_cap, size_t *out_len);

/**
 * @brief Write a complete ACK message (header and payload).
 * @param[in]  ack      ACK to encode.
 * @param[out] out      Destination buffer.
 * @param[in]  out_cap  Size of @p out in bytes.
 * @param[out] out_len  Bytes written (WIRE_ACK_MSG_LEN); 0 on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_BUFFER_TOO_SMALL if @p out_cap is too small,
 *         PQC_ERR_WIRE_BAD_FIELD if @p ack has seq 0.
 */
int wire_encode_ack(const struct wire_ack *ack, uint8_t *out, size_t out_cap,
                    size_t *out_len);

/**
 * @brief Check a received message header before reading the payload.
 * @param[in]  hdr   The WIRE_HEADER_LEN header bytes.
 * @param[out] type  Message type (WIRE_TYPE_RECORD or WIRE_TYPE_ACK).
 * @param[out] len   Payload length the sender announced.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_WIRE_BAD_TYPE for an unknown type,
 *         PQC_ERR_WIRE_BAD_LENGTH if the length is above WIRE_MAX_PAYLOAD or
 *         is not the exact length for that type.
 * @warning Call this before allocating or reading a payload: a length is never
 *          trusted until it has passed this check. On any error the receiver
 *          must close the connection and count the event.
 */
int wire_parse_header(const uint8_t *hdr, uint8_t *type, uint16_t *len);

/**
 * @brief Decode a RECORD payload.
 * @param[in]  payload  Payload bytes (without the header).
 * @param[in]  len      Number of bytes in @p payload.
 * @param[out] rec      Decoded record; zeroed on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_WIRE_BAD_LENGTH if @p len is not WIRE_RECORD_LEN,
 *         PQC_ERR_WIRE_BAD_FIELD if seq is 0.
 */
int wire_decode_record(const uint8_t *payload, size_t len,
                       struct wire_record *rec);

/**
 * @brief Decode an ACK payload.
 * @param[in]  payload  Payload bytes (without the header).
 * @param[in]  len      Number of bytes in @p payload.
 * @param[out] ack      Decoded ACK; zeroed on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_WIRE_BAD_LENGTH if @p len is not WIRE_ACK_LEN,
 *         PQC_ERR_WIRE_BAD_FIELD if seq is 0.
 */
int wire_decode_ack(const uint8_t *payload, size_t len, struct wire_ack *ack);

#endif /* PQC_WIRE_H */
