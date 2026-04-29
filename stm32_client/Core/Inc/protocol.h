#ifndef PROTOCOL_H
#define PROTOCOL_H

void protocol_init(void);
void protocol_process(void);
void protocol_handle_line(const char *line);

#endif