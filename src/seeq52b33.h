/* SEEQ 52B33 custom T48 driver. GPL-3.0-or-later; see LICENSE. */
#ifndef SEEQ52B33_H_
#define SEEQ52B33_H_

int seeq52b33_validate(minipro_handle_t *);
int seeq52b33_init(minipro_handle_t *);
int seeq52b33_terminate(minipro_handle_t *);
int seeq52b33_read(minipro_handle_t *, uint32_t, uint8_t *, size_t);
int seeq52b33_write(minipro_handle_t *, uint32_t, const uint8_t *, size_t);
int seeq52b33_erase(minipro_handle_t *);

#endif
