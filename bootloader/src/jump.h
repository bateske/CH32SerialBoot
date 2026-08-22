#ifndef CHGAME_JUMP_H
#define CHGAME_JUMP_H

/* Hands control to the application at CHGAME_APP_START. Never returns.
 * Callers must have already established that the application is valid. */
void jump_to_app(void) __attribute__((noreturn));

#endif
