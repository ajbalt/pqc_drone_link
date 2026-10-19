#ifndef PQC_UART_FRAME_H
#define PQC_UART_FRAME_H

/**
 * @file uart_frame.h
 * @brief The UART frame between Pico A and Pi A: encoder, sample packing and a
 *        resynchronising parser. Shared by the Pico firmware and the Pi code.
 *
 * Layout and limits are defined in docs/telemetry_format.md section 2:
 *
 *     sync (2) = A5 5A | len (1) | payload (len) | crc16 (2)
 *
 * All multi-byte fields are big-endian. The CRC is CRC-16/CCITT-FALSE over the
 * `len` byte and the payload.
 *
 * The file has no operating-system, heap or stdio dependency. It uses only
 * <stdint.h>, <stddef.h> and common/errors.h, so the Pico SDK build can
 * compile uart_frame.c as it is (add the repo's src/ directory to the include
 * path). Nothing here allocates: the parser lives in a struct the caller owns.
 * (As in any C program, the compiler may turn a copy loop into a memmove call.)
 *
 * @note The CRC catches accidental corruption on the wire. It is not security:
 *       the UART is a cable between two boards we own, and TLS protects the
 *       Wi-Fi link.
 */

#include <stddef.h>
#include <stdint.h>

/** @brief First sync byte. */
#define UART_FRAME_SYNC0 0xA5
/** @brief Second sync byte. */
#define UART_FRAME_SYNC1 0x5A
/** @brief Largest payload a frame may carry (proposed in the spec). */
#define UART_FRAME_MAX_PAYLOAD 32
/** @brief Bytes around the payload: sync (2), len (1) and crc16 (2). */
#define UART_FRAME_OVERHEAD 5
/** @brief Bytes in the longest frame. */
#define UART_FRAME_MAX_LEN (UART_FRAME_OVERHEAD + UART_FRAME_MAX_PAYLOAD)
/** @brief Bytes in a sensor sample payload: pico_ms (4) and six values (12). */
#define UART_SAMPLE_LEN 16

/** @brief One sensor sample as it travels in a frame (spec section 2). */
struct uart_sample {
  uint32_t pico_ms; /**< ms since Pico A booted; wraps at 2^32 */
  int16_t imu[6];   /**< ax ay az gx gy gz, signed raw LSB */
};

/** @brief What the parser found. */
enum uart_event_type {
  UART_EVENT_NONE = 0, /**< nothing more to report until another byte arrives */
  UART_EVENT_FRAME,    /**< a frame with a good CRC; see uart_event */
  UART_EVENT_BAD_LENGTH, /**< a header with a length that is not allowed */
  UART_EVENT_BAD_CRC,    /**< a complete frame whose CRC does not match */
};

/** @brief One result from uart_parser_poll(). */
struct uart_event {
  enum uart_event_type type; /**< what happened */
  const uint8_t *payload;    /**< UART_EVENT_FRAME only: the payload bytes */
  size_t len;                /**< UART_EVENT_FRAME only: payload length */
};

/** @brief Counters, for the evidence files. They wrap at 2^32. */
struct uart_stats {
  uint32_t frames_ok;   /**< frames delivered */
  uint32_t bad_length;  /**< headers with a length that is not allowed */
  uint32_t bad_crc;     /**< complete frames with a wrong CRC */
  uint32_t noise_bytes; /**< bytes skipped while looking for a frame start */
};

/**
 * @brief The parser. Declared here so the caller can allocate it statically
 *        (on the Pico) or on the stack; treat every field as private.
 */
struct uart_parser {
  uint8_t win[UART_FRAME_MAX_LEN]; /**< bytes received and not yet judged */
  uint8_t fill;                    /**< bytes in win */
  uint8_t drop;                    /**< bytes to remove before the next call */
  uint8_t expect_len;              /**< payload length wanted, 0 = any */
  struct uart_stats stats;         /**< counters */
};

/**
 * @brief CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no
 *        reflection, no final XOR.
 * @param[in] p  Bytes to check; may be NULL only if @p n is 0.
 * @param[in] n  Number of bytes.
 * @return The CRC (0xFFFF for no bytes; 0x29B1 for "123456789").
 */
uint16_t uart_frame_crc16(const uint8_t *p, size_t n);

/**
 * @brief Build a complete frame around @p payload.
 * @param[in]  payload  Payload bytes.
 * @param[in]  len      Payload length, 1 to UART_FRAME_MAX_PAYLOAD.
 * @param[out] out      Destination for the frame.
 * @param[in]  out_cap  Size of @p out in bytes.
 * @param[out] out_len  Bytes written (len + UART_FRAME_OVERHEAD); 0 on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer or a @p len outside
 *         1 to UART_FRAME_MAX_PAYLOAD, PQC_ERR_BUFFER_TOO_SMALL if @p out_cap
 *         is smaller than the frame.
 */
int uart_frame_encode(const uint8_t *payload, size_t len, uint8_t *out,
                      size_t out_cap, size_t *out_len);

/**
 * @brief Write a sample as a UART_SAMPLE_LEN-byte payload, big-endian.
 * @param[in]  s    Sample to pack.
 * @param[out] out  Destination, at least UART_SAMPLE_LEN bytes.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int uart_sample_pack(const struct uart_sample *s, uint8_t *out);

/**
 * @brief Read a sample from a payload.
 * @param[in]  payload  Payload bytes (without the frame around them).
 * @param[in]  len      Number of bytes in @p payload.
 * @param[out] s        Decoded sample; zeroed on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_WIRE_BAD_LENGTH if @p len is not UART_SAMPLE_LEN.
 */
int uart_sample_unpack(const uint8_t *payload, size_t len,
                       struct uart_sample *s);

/**
 * @brief Prepare a parser, and clear its counters.
 * @param[out] p                 The parser.
 * @param[in]  expected_payload_len  The only payload length that is accepted
 *                               (UART_SAMPLE_LEN for the sensor stream), or 0
 *                               to accept any length from 1 to
 *                               UART_FRAME_MAX_PAYLOAD.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for NULL or an expected length above
 *         UART_FRAME_MAX_PAYLOAD.
 */
int uart_parser_init(struct uart_parser *p, size_t expected_payload_len);

/**
 * @brief Give the parser one received byte.
 *
 * Call uart_parser_poll() until it reports UART_EVENT_NONE before pushing the
 * next byte, so the window never fills.
 *
 * @param[in,out] p     The parser.
 * @param[in]     byte  The byte just read from the UART.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for NULL, PQC_ERR_BUFFER_TOO_SMALL if
 *         the window is full because events were not polled.
 */
int uart_parser_push(struct uart_parser *p, uint8_t byte);

/**
 * @brief Report the next thing found in the bytes pushed so far.
 *
 * Call it after every push, and again until @p ev->type is UART_EVENT_NONE:
 * one byte can finish one frame and also expose a second one hiding behind a
 * damaged one. On a bad length or a bad CRC the first byte of the damaged frame
 * is dropped and the rest is searched again, so a lost byte, an extra byte or
 * a false sync costs only the damaged frame (docs/telemetry_format.md section
 * 2).
 *
 * @param[in,out] p   The parser.
 * @param[out]    ev  The event; its type is UART_EVENT_NONE if there is none.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 * @warning For UART_EVENT_FRAME, ev->payload points into the parser's window
 *          and is valid only until the next uart_parser_push() or
 *          uart_parser_poll(). Copy it (for example with uart_sample_unpack())
 *          first.
 */
int uart_parser_poll(struct uart_parser *p, struct uart_event *ev);

/**
 * @brief Read the counters.
 * @param[in]  p    The parser.
 * @param[out] out  Counters; the number of bad frames (`uart_bad_frames` in
 *                  the spec) is bad_length + bad_crc.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int uart_parser_stats(const struct uart_parser *p, struct uart_stats *out);

#endif /* PQC_UART_FRAME_H */
