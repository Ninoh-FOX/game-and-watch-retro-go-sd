#ifndef __HW_SHA1_H
#define __HW_SHA1_H

/* Get sha1 value in output (uint8_t [20]) */
int8_t calculate_sha1_hw(const uint8_t *data, size_t len, uint8_t *output);

#endif /* __HW_SHA1_H */
