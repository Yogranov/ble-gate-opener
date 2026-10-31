/* The USB console, for first setup, a forgotten password or tests from a PC. */
#pragma once

/* register_more adds commands that belong to other components. It may be NULL. */
void yg_console_start(void (*register_more)(void));
