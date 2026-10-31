// keyboard.h - the flick / toggle kana keyboard (v3.13+).
#ifndef KEYBOARD_H
#define KEYBOARD_H

// Runs the keyboard until the user confirms (returns 0, query
// in g_query) or cancels (returns -1).
int kana_prompt(void);

#endif // KEYBOARD_H
