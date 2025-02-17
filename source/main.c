/*
 * Shenzhen Solitaire Game Gear
 *
 * A Shenzhen I/O Solitaire clone for the Sega Game Gear
 */

#define TARGET_GG

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SMSlib.h"

#include "rng.h"
#include "patterns.c"



/* Constants */
#define PORT_A_KEY_DPAD     (PORT_A_KEY_UP | PORT_A_KEY_DOWN | PORT_A_KEY_LEFT | PORT_A_KEY_RIGHT)
#define CARD_TYPE_MASK      0x30
#define CARD_VALUE_MASK     0x0f
#define STACK_HELD          15

/* Palette */
const uint8_t palette [16] = {
    0x94,   /*  0 - (table) Dark green */
    0x19,   /*  1 - (table) Light green */
    0x00,   /*  2 - (card / cursor) Black */
    0x3f,   /*  3 - (card / cursor) White */
    0xa9,//0x02,   /*  4 - (card) Red */
    0x09,//0x09,   /*  5 - (card / menu) Green */
    0xd9,   /*  6 - (cursor) Light grey */      //fill the card color
    0xb1,//0x15,   /*  7 - (cursor) Dark grey */  //outline the card
    0xc4,   /*  8 - (menu) Blue */
    0x16,   /*  9 - (menu) Brick */
    0x04,   /* 10 - (menu) Dark green */
};

//0e = red
//3b=orange
//3f=bright red
//94=light green
//a9=dark yellow

bool sprite_update = false;
uint8_t cursor_style = 1;
    bool in_menu = true;

/* Card bits:
 *   [6:7] Zero
 *   [4:5] Card type (0:black, 1:red, 2:green, 3:special)
 *   [0:3] Card value:
 *         0-8: Numbers 1-9
 *         0-2: Claw, paw, hoof prints
 *         3  : Snep
 *  0xff: End of stack.
 */

uint8_t deck [] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x30, 0x30, 0x30, 0x30, 0x31, 0x31, 0x31, 0x31, 0x32,
    0x32, 0x32, 0x32, 0x33
};


uint8_t stack [16] [16] = {
    { 0xff }, { 0xff }, { 0xff }, { 0xff },
    { 0xff }, { 0xff }, { 0xff }, { 0xff },
    { 0xff }, { 0xff }, { 0xff }, { 0xff },
    { 0xff }, { 0xff }, { 0xff }, { 0xff }
};
bool stack_changed [16] = { false };
uint8_t came_from = 0xff;

bool button_active [3] = { false };

/* Cursor */
enum cursor_stack_e
{
    CURSOR_COLUMN_1 = 0,
    CURSOR_COLUMN_2,//1
    CURSOR_COLUMN_3,//2
    CURSOR_COLUMN_4,//3
    CURSOR_COLUMN_5,//4
    CURSOR_COLUMN_6,//5
    CURSOR_COLUMN_7,//6
    CURSOR_COLUMN_8,//7
    CURSOR_DRAGON_SLOT_1,//8
    CURSOR_DRAGON_SLOT_2,//9
    CURSOR_DRAGON_SLOT_3,//10
    CURSOR_DRAGON_BUTTONS,//11
    CURSOR_FOUNDATION_SNEP,//12
    CURSOR_FOUNDATION_1,//13
    CURSOR_FOUNDATION_2,//14
    CURSOR_FOUNDATION_3,//15
    CURSOR_STACK_MAX
};

#define CURSOR_DEPTH_MAX 15
uint8_t cursor_stack = CURSOR_COLUMN_1;
uint8_t cursor_depth = CURSOR_DEPTH_MAX;

unsigned char scrollX;
void manageScroll();
/*
 * Calculate the index of the top card in the selected stack.
 */
uint8_t top_card (uint8_t s)
{
    uint8_t depth;

    if (stack [s] [0] == 0xff)
    {
        return 0;
    }

    for (depth = 0; depth < CURSOR_DEPTH_MAX; depth++)
    {
        if (stack [s] [depth + 1] == 0xff)
        {
            break;
        }
    }

    return depth;
}


/*
 * Check the any dragon buttons are active.
 */
void check_dragons (void)
{
    bool empty_slot = false;
    bool in_slot [3] = { false };
    uint8_t count [3] = { 0 };

    /* Check for empty slots */
    if (stack [CURSOR_DRAGON_SLOT_1] [0] == 0xff ||
        stack [CURSOR_DRAGON_SLOT_2] [0] == 0xff ||
        stack [CURSOR_DRAGON_SLOT_3] [0] == 0xff)
    {
        empty_slot = true;
    }

    for (uint8_t stack_idx = 0; stack_idx <= CURSOR_DRAGON_SLOT_3; stack_idx++)
    {
        uint8_t card = stack [stack_idx] [top_card (stack_idx)];

        for (uint8_t kind = 0; kind < 3; kind++)
        {
            if (card == (0x30 + kind))
            {
                count [kind]++;

                if (stack_idx >= CURSOR_DRAGON_SLOT_1)
                {
                    in_slot [kind] = true;
                }
            }
        }
    }

    /* Light up the button if all of a kind are visible and have somewhere to go */
    for (uint8_t kind = 0; kind < 3; kind++)
    {
        button_active [kind] = (count [kind] == 4) && (empty_slot || in_slot [kind]);
    }
}


/*
 * Render a single card to an array of tile indexes.
 */
void render_card_tiles (uint16_t *buf, uint8_t card, bool stacked)
{
    uint8_t value = card & CARD_VALUE_MASK;
    uint8_t tile;

    uint16_t card_tiles [] = {
        BLANK_CARD +  0, BLANK_CARD +  2, BLANK_CARD +  2, BLANK_CARD +  3,
//        BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
//        BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        BLANK_CARD +  8, BLANK_CARD +  9, BLANK_CARD +  9, BLANK_CARD + 10
    };

/*
[1] 0,1,2,3
[2] 4,5,6,7
[3] 8,9,10,11
[4] 12,13,14,15
[5] 16,17,18,19
[6] 20,21,22,23
*/

    if ((card & CARD_TYPE_MASK) == 0x30)
    {
        if (value == 3)
        {
            /* Snep card */
            card_tiles [0]  = CORNER_SNEP;
            card_tiles [15] = CORNER_SNEP + 2;// card_tiles [23] = CORNER_SNEP + 2;

            for (uint8_t i = 0; i < 12; i++)//16
            {
                card_tiles [i + 4] = ARTWORK_SNEP + i;//4
            }
        }
        else
        {
            /* Print card */
            card_tiles [0]  = CORNER_PRINTS + (value * 3);
            card_tiles [1]  = CORNER_PRINTS + (value * 3) + 2;

            tile = ARTWORK_PRINTS + value * 4;
            card_tiles [5] = tile;//9
            card_tiles [6] = tile + 1;//10
            card_tiles [9] = tile + 2;//13
            card_tiles [10] = tile + 3;//14
        }
    }
    else
    {
        /* Standard card */
        uint8_t colour = card >> 4;

        /* Card corners */
        tile = CORNER_NUMBERS + value * 6 + colour * 2;
        card_tiles [ 0] = tile;

        /* Chinese numbers */
        tile = ARTWORK_NUMBERS + value * 12 + colour * 4;
        card_tiles [5] = tile;//9
        card_tiles [6] = tile + 1;//10
        card_tiles [9] = tile + 2;//13
        card_tiles [10] = tile + 3;//14
    }

    if (stacked)
    {
        /* Show top of card below */
        card_tiles [0] += 1;
        card_tiles [3] += 1;
    }

    memcpy (buf, card_tiles, sizeof (card_tiles));
}


/*
 * Render the cursor and its held cards, as sprites.
 * Position specified as (x, y) coordinate.
 */
void cursor_render_xy (uint8_t cursor_x, uint8_t cursor_y, bool cursor_visible)
{
    /* Clear any previous sprites */
    SMS_initSprites ();

    if (cursor_visible)
    {
        SMS_addSprite (cursor_x,     cursor_y,     (uint8_t) (CURSOR_BLACK + (4 * cursor_style)    ));
        SMS_addSprite (cursor_x + 8, cursor_y,     (uint8_t) (CURSOR_BLACK + (4 * cursor_style) + 1));
        SMS_addSprite (cursor_x,     cursor_y + 8, (uint8_t) (CURSOR_BLACK + (4 * cursor_style) + 2));
        SMS_addSprite (cursor_x + 8, cursor_y + 8, (uint8_t) (CURSOR_BLACK + (4 * cursor_style) + 3));
    }

    /* Render held cards as sprites */
    if (stack [STACK_HELD] [0] != 0xff)
    {
        uint8_t card_x = cursor_x - 16;
        uint8_t top = top_card (STACK_HELD);

        for (uint8_t i = top; i != 0xff; i--)
        {
            uint16_t card_y = cursor_y + (8 * i) - 4;
            uint16_t card_tiles [16];//24

            render_card_tiles (card_tiles, stack [STACK_HELD] [i], i > 0);

            for (uint8_t y = 0; y < 4; y++)//6
            {
                uint16_t sprite_y = card_y + (8 * y);

#define screen_max_Y 144

                /* Don't draw sprites that are completely off screen */
                if (sprite_y > screen_max_Y)
                {
                    continue;
                }

                for (uint8_t x = 0; x < 4; x++)
                {
//SMS_addSprite (unsigned char x, unsigned char y, unsigned char tile);  /* declare a sprite - returns handle or -1 if no more sprites are available */
                    SMS_addSprite (card_x + (8 * x), sprite_y, (uint8_t) card_tiles [x + (4 * y)]);
                }

                /* Only the top card is fully drawn */
                if (i != top)
                {
                    break;
                }
            }
        }
    }

    sprite_update = true;
}


/*
 * Convert cursor (stack, depth) coordinates into (x, y).
 */
void cursor_sd_to_xy (uint8_t stack, uint8_t depth, uint8_t *x, uint8_t *y)
{
   // *x = (stack & 0x07) * 32 + 16;

if(cursor_stack==CURSOR_COLUMN_1  || cursor_stack==CURSOR_DRAGON_SLOT_1)*x = 58;
else if(cursor_stack==CURSOR_COLUMN_2 || cursor_stack==CURSOR_DRAGON_SLOT_2)*x = 80;
else if(cursor_stack==CURSOR_COLUMN_3 || cursor_stack==CURSOR_DRAGON_SLOT_3)*x = 102;
else if(cursor_stack==CURSOR_COLUMN_4 || cursor_stack==CURSOR_DRAGON_BUTTONS)*x = 128;
else if(cursor_stack==CURSOR_COLUMN_5 || cursor_stack==CURSOR_FOUNDATION_SNEP)*x = 148;
else if(cursor_stack==CURSOR_COLUMN_6 || cursor_stack==CURSOR_FOUNDATION_1)*x = 151;
else if(cursor_stack==CURSOR_COLUMN_7 || cursor_stack==CURSOR_FOUNDATION_2)*x = 172;
else if(cursor_stack==CURSOR_COLUMN_8 || cursor_stack==CURSOR_FOUNDATION_3)*x = 184;

if(in_menu==true)*x = (stack & 0x07) * 32 + 16;

    if (stack == CURSOR_DRAGON_BUTTONS)
    {
        *y = (depth * 16) + 30;//+16
    }
    else if (stack > CURSOR_COLUMN_8)
    {
        *y = 44;//12 default. 20 works well
    }
    else
    {
        *y = (depth * 8) + 86;//7,88 -> this changes where cursor falls on cards
    }
}


/*
 * Render the cursor and its held cards, as sprites.
 * Position specified as (stack, depth) coordinate.
 */
void cursor_render (void)
{
    uint8_t cursor_x;
    uint8_t cursor_y;

    cursor_sd_to_xy (cursor_stack, cursor_depth, &cursor_x, &cursor_y);

    /* Offset the cursor if we're holding a card */
    if (stack [STACK_HELD] [0] != 0xff)
    {
        cursor_x += 2;
        cursor_y += 12;
    }


    cursor_render_xy (cursor_x, cursor_y, true);
}


/*
 * Calculate the new cursor position after d-pad input.
 */
void cursor_move (uint8_t direction)
{
    uint8_t stack_max_depth = 0;
    uint8_t stack_idx;

    /* First, perform the motion */
    switch (direction)
    {
        case PORT_A_KEY_LEFT:
            cursor_stack = (cursor_stack + (CURSOR_STACK_MAX - 1)) % CURSOR_STACK_MAX;
            cursor_depth = CURSOR_DEPTH_MAX;

            if (cursor_stack == CURSOR_DRAGON_BUTTONS)
            {
                if (stack [STACK_HELD] [0] != 0xff)
                {
                    /* Skip over the buttons if holding a card */
                    cursor_stack--;
                }
                else
                {
                    /* Start on the top button */
                    cursor_depth = 0;
                }
            }

            break;

        case PORT_A_KEY_RIGHT:
            cursor_stack = (cursor_stack + 1) % CURSOR_STACK_MAX;
            cursor_depth = CURSOR_DEPTH_MAX;

            /* Skip over the buttons if holding a card */
            if (cursor_stack == CURSOR_DRAGON_BUTTONS)
            {
                if (stack [STACK_HELD] [0] != 0xff)
                {
                    /* Skip over the buttons if holding a card */
                    cursor_stack++;
                }
                else
                {
                    /* Start on the top button */
                    cursor_depth = 0;
                }
            }
            break;

        case PORT_A_KEY_UP:
            if (cursor_depth > 0 &&
                    ((cursor_stack == CURSOR_DRAGON_BUTTONS) ||
                     (cursor_stack <= CURSOR_COLUMN_8 && stack [STACK_HELD] [0] == 0xff)))
            {
                cursor_depth--;
            }
            break;

        case PORT_A_KEY_DOWN:
            cursor_depth++;
            break;
    }

    /* Next, calculate the maximum depth for the column */
    stack_idx = (cursor_stack < CURSOR_DRAGON_BUTTONS) ? cursor_stack : cursor_stack - 1;
    if (cursor_stack == CURSOR_DRAGON_BUTTONS)
    {
        stack_max_depth = 2;
    }
    else if (stack [stack_idx] [0] != 0xff)
    {
        stack_max_depth = top_card (stack_idx);
    }

    /* Enforce the limit */
    if (cursor_depth > stack_max_depth)
    {
        cursor_depth = stack_max_depth;
    }

if (in_menu==true)
{
	if(cursor_stack <2)cursor_stack=2;
	if(cursor_stack >4)cursor_stack=4;
}

    cursor_render ();
}


/*
 * Pick up the selected card.
 */
void cursor_pick (void)
{
    uint8_t i;
    uint8_t stack_idx = (cursor_stack < CURSOR_DRAGON_BUTTONS) ? cursor_stack : cursor_stack - 1;

    /* Check if the selected cards can be picked up together */
    if (stack [stack_idx] [cursor_depth + 1] != 0xff)
    {
        uint8_t previous_card = 0;
        for (i = 0; stack [stack_idx] [cursor_depth + i] != 0xff; i++)
        {
            uint8_t card = stack [stack_idx] [cursor_depth + i];

            /* Special cards cannot be stacked */
            if ((card & CARD_TYPE_MASK) == 0x30)
            {
                return;
            }

            if (i > 0)
            {
                if (cursor_stack <= CURSOR_COLUMN_8)
                {
                    /* Colours must alternate */
                    if ((card & CARD_TYPE_MASK) == (previous_card & CARD_TYPE_MASK))
                    {
                        return;
                    }

                    /* Value must decrease */
                    if ((card & CARD_VALUE_MASK) != (previous_card & CARD_VALUE_MASK) - 1)
                    {
                        return;
                    }
                }
            }

            previous_card = card;
        }
    }

    /* Once a stack of dragons is stored, it stays */
    if (cursor_stack >= CURSOR_DRAGON_SLOT_1 && cursor_stack <= CURSOR_DRAGON_SLOT_3)
    {
        if (cursor_depth > 0)
        {
            return;
        }
    }

    /* Move the selected stack into the hand */
    for (i = 0; stack [stack_idx] [cursor_depth + i] != 0xff; i++)
    {
        stack [STACK_HELD] [i] = stack [stack_idx] [cursor_depth + i];
        stack [stack_idx] [cursor_depth + i] = 0xff;
    }
    stack [STACK_HELD] [i] = 0xff;
    stack_changed [stack_idx] = true;

    came_from = cursor_stack;

    /* Point at the new top card in the stack */
    cursor_depth = CURSOR_DEPTH_MAX;
    cursor_move (PORT_A_KEY_DOWN);
}


/*
 * Place the selected card.
 */
void cursor_place (void)
{
    uint8_t i;
    uint8_t stack_idx = (cursor_stack < CURSOR_DRAGON_BUTTONS) ? cursor_stack : cursor_stack - 1;

    /* Check if cards are allowed to move here */
    if (cursor_stack != came_from)
    {
        uint8_t stack_card = stack [stack_idx] [cursor_depth];

        if (cursor_stack <= CURSOR_COLUMN_8)
        {
            if (stack_card != 0xff)
            {
                /* Special cards cannot be stacked */
                if (((stack_card             & 0x30) == 0x30) ||
                    ((stack [STACK_HELD] [0] & 0x30) == 0x30))
                {
                    return;
                }

                /* Colours must alternate */
                if ((stack_card & 0x30) == (stack [STACK_HELD] [0] & 0x30))
                {
                    return;
                }

                /* Value must decrease */
                if ((stack_card & CARD_VALUE_MASK) != (stack [STACK_HELD] [0] & CARD_VALUE_MASK) + 1)
                {
                    return;
                }
            }
        }
        else if (cursor_stack <= CURSOR_DRAGON_SLOT_3)
        {
            /* Only single cards may be placed in the dragon slots */
            if ((stack_card != 0xff) || (stack [STACK_HELD] [1] != 0xff))
            {
                return;
            }
        }
        else if (cursor_stack <= CURSOR_DRAGON_BUTTONS)
        {
            /* Not a card slot */
            return;
        }
        else if (cursor_stack <= CURSOR_FOUNDATION_SNEP)
        {
            /* Only the snep card may be placed in the snep card slot */
            if (stack [STACK_HELD] [0] != 0x33)
            {
                return;
            }
        }
        else if (cursor_stack <= CURSOR_FOUNDATION_3)
        {
            /* No special cards, and only one card at a time */
            if ((stack [STACK_HELD] [0] & CARD_TYPE_MASK) == 0x30 || stack [STACK_HELD] [1] != 0xff)
            {
                return;
            }

            /* Only a '1' can be placed on an empty slot */
            if (stack [stack_idx] [0] == 0xff)
            {
                if ((stack [STACK_HELD] [0] & CARD_VALUE_MASK) != 0)
                {
                    return;
                }
            }
            else
            {
                /* Cards in a foundation must all be the same colour */
                if ((stack_card & CARD_TYPE_MASK) != (stack [STACK_HELD] [0] & CARD_TYPE_MASK))
                {
                    return;
                }
                /* Cards in a foundation must be in increasing order */
                if ((stack_card & CARD_VALUE_MASK) != (stack [STACK_HELD] [0] & CARD_VALUE_MASK) - 1)
                {
                    return;
                }
            }
        }
        else
        {
            /* Invalid */
            return;
        }

    }

    /* Place at the first empty slot, not the last full slot */
    if (stack [stack_idx] [0] != 0xff)
    {
        cursor_depth++;
    }

    /* Move the cards from the hand */
    for (i = 0; stack [STACK_HELD] [i] != 0xff; i++)
    {
        stack [stack_idx] [cursor_depth + i] = stack [STACK_HELD] [i];
        stack [STACK_HELD] [i] = 0xff;
    }
    stack [stack_idx] [cursor_depth + i] = 0xff;
    stack_changed [stack_idx] = true;

    came_from = 0xff;

    /* Point at the new top card in the stack */
    cursor_depth = CURSOR_DEPTH_MAX;
    cursor_move (PORT_A_KEY_DOWN);
}


/*
 * Render one card to the background.
 */
void render_card_background (uint8_t col, uint8_t y, uint8_t card, bool stacked, bool covered)
{
    uint16_t card_tiles [16];//24

    render_card_tiles (card_tiles, card, stacked);

    SMS_loadTileMapArea ((4 * col), y+1, &card_tiles, 4, covered ? 1 : 4);//last parameter 6->4    y+1
//the cards in the playing field
}

/*
 * Renders the cards.
 */
void render_background (void)
{
    uint16_t blank_line [] = {
        EMPTY_TILE, EMPTY_TILE, EMPTY_TILE, EMPTY_TILE
    };
    uint16_t empty_slot [] = {
        OUTLINE_CARD + 0, OUTLINE_CARD + 1, OUTLINE_CARD + 1, OUTLINE_CARD + 2,
        //OUTLINE_CARD + 3, EMPTY_TILE,       EMPTY_TILE,       OUTLINE_CARD + 4,
        OUTLINE_CARD + 3, EMPTY_TILE,       EMPTY_TILE,       OUTLINE_CARD + 4,
        OUTLINE_CARD + 3, EMPTY_TILE,       EMPTY_TILE,       OUTLINE_CARD + 4,
        //OUTLINE_CARD + 3, EMPTY_TILE,       EMPTY_TILE,       OUTLINE_CARD + 4,
        OUTLINE_CARD + 5, OUTLINE_CARD + 6, OUTLINE_CARD + 6, OUTLINE_CARD + 7
    };
    uint16_t button_tiles [4];

    /* Dragons & Foundations*/
    for (uint8_t i = 0; i < 7; i++)
    {
        uint8_t col = (i < 3) ? i : i + 1;

        if (!stack_changed [8 + i])
        {
            continue;
        }

        if (stack [i + 8] [0] != 0xff)//these are cards dropped into the top row
        {
            uint8_t depth = top_card (i + 8);
            render_card_background (col, 3, stack [i + 8] [depth], false, false);//2nd param 2
        }
        else
        {
            SMS_loadTileMapArea ((4 * col), 4, &empty_slot, 4, 4);//last parameter 6->4, 2nd parameter 3
        }
    }

    /* Tableau columns */
    for (int col = 0; col < 8; col++)
    {
        uint8_t depth;

        if (!stack_changed [col])
        {
            continue;
        }

        if (stack [col] [0] == 0xff)
        {
            SMS_loadTileMapArea ((4 * col), 10, &empty_slot, 4, 4);//last parameter 6->4, 2nd param 9->8, 8 works well
            depth = 1;
        }
        else
        {
            for (depth = 0; depth < 13; depth++)
            {
                uint8_t card = stack [col] [depth];
                uint8_t next = stack [col] [depth + 1];

                if (card == 0xff)
                {
                    break;
                }

                render_card_background (col, 9 + depth, card, depth, next != 0xff);//9
//cards in the field
            }
        }

        /* Clear area below stack */
        depth += 4;//5
        while (depth < 18)//18
        {//loadTileMapArea (unsigned char x, unsigned char y,  unsigned int *src, unsigned char width, unsigned char height);
            SMS_loadTileMapArea ((4 * col), 9 + depth, &blank_line, 4, 1);//9+depth
            depth++;
        }
    }

    for (uint8_t i = 0; i < sizeof (stack_changed); i++)
    {
        if (stack_changed [i])
        {
            check_dragons ();
            break;
        }
    }

    /* Buttons */
    for (uint8_t i = 0; i < 3; i++)
    {
        button_tiles [0] = BUTTON_TILES + (i * 8) + (button_active [i] * 4);
        button_tiles [1] = BUTTON_TILES + (i * 8) + (button_active [i] * 4) + 1;
        button_tiles [2] = BUTTON_TILES + (i * 8) + (button_active [i] * 4) + 2;
        button_tiles [3] = BUTTON_TILES + (i * 8) + (button_active [i] * 4) + 3;

        SMS_loadTileMapArea (13, (i * 2) + 3, &button_tiles, 2, 2);//(i * 2) + 1
    }

    memset (stack_changed, false, sizeof (stack_changed));
}


/*
 * Animate a card sliding from one position to another.
 */
void card_slide (uint16_t start_x, uint16_t start_y,
                 uint16_t end_x,   uint16_t end_y,
                 uint8_t frames,   bool cursor_visible)
{
    uint16_t x;
    uint16_t y;

    for (uint8_t frame = 1; frame < frames; frame++)
    {
        /* Calculate next position */
        x = (((start_x * 8) * (frames - frame)) +
             ((end_x   * 8) * (         frame))) / frames / 8;
        y = (((start_y * 8) * (frames - frame)) +
             ((end_y   * 8) * (         frame))) / frames / 8;

        cursor_render_xy (x, y, cursor_visible);

        /* Write to hardware */
        SMS_waitForVBlank ();
        SMS_copySpritestoSAT ();
    }

    SMS_initSprites ();
    SMS_copySpritestoSAT ();
}


/*
 * Deal a new game.
 */
void deal (void)
{
    uint8_t i;

    rng_seed ();

    for (i = 0; i < 16; i++)
    {
        stack [i] [0] = 0xff;
    }
    memset (stack_changed, true, sizeof (stack_changed));
    render_background ();

    /* Shuffle the deck */
    for (i = 39; i >= 1; i--)
    {
        uint8_t temp = deck [i];
        uint8_t swap_i = rand () % (i + 1);

        deck [i] = deck [swap_i];
        deck [swap_i] = temp;
    }

    /* Place the cards */
    i = 0;
    stack [STACK_HELD] [1] = 0xff;
    for (uint8_t depth = 0; depth < 5; depth++)
    {
        for (uint8_t col = 0; col < 8; col++)
        {
            uint8_t dest_x;
            uint8_t dest_y;

            cursor_sd_to_xy (col, depth, &dest_x, &dest_y);

dest_x=(col+1)*32;

            /* Animate the card being dealt */
            stack [STACK_HELD] [0] = deck [i];
            card_slide (dest_x, 144+24, dest_x, dest_y, 8, false);//2nd paramter is where it comes from
            stack [STACK_HELD] [0] = 0xff;

            /* Store the card in its new position */
            stack [col] [depth] = deck [i++];
            stack [col] [depth + 1] = 0xff;
            stack_changed [col] = true;

            render_background ();
        }
    }

    cursor_stack = CURSOR_COLUMN_6;//CURSOR_COLUMN_1;
    cursor_depth = CURSOR_DEPTH_MAX;
    cursor_move (PORT_A_KEY_DOWN);
}


/*
 * Undeal the cards (winning animation).
 */
void undeal (void)
{
    bool cards_left = true;
    stack [STACK_HELD] [1] = 0xff;

    while (cards_left)
    {
        cards_left = false;

        for (uint8_t col = CURSOR_DRAGON_SLOT_1; col <= CURSOR_FOUNDATION_3; col++)
        {
            uint8_t stack_idx = (col < CURSOR_DRAGON_BUTTONS) ? col : col - 1;
            uint8_t from_x;
            uint8_t from_y;
            uint8_t top;

            if (col == CURSOR_DRAGON_BUTTONS)
            {
                continue;
            }

            top = top_card (stack_idx);

            if (stack [stack_idx] [top] == 0xff)
            {
                continue;
            }

            cards_left = true;


            cursor_sd_to_xy (col, top, &from_x, &from_y);

            /* Animate the card being removed */
            stack [STACK_HELD] [0] = stack [stack_idx] [top];
            stack [stack_idx] [top] = 0xff;
            stack_changed [stack_idx] = true;

            render_background ();
            card_slide (from_x, from_y, from_x, 144+24, 8, false);//192

            stack [STACK_HELD] [0] = 0xff;
        }
    }
}

/*
 * Fill the name table with tile-zero.
 */
void clear_background (void)
{
    uint16_t blank_line [32] = { 0 };//32

//256/8=32
//160/8=20

//224/8=28
//144/8=18

    for (uint8_t row = 0; row < 18; row++)//24
    {
//SMS_loadTileMapArea (unsigned char x, unsigned char y,  unsigned int *src, unsigned char width, unsigned char height);
        SMS_loadTileMapArea (0, row, &blank_line, 28, 1);//32
    }
}


/*
 * Stack four dragon cards into a slot.
 */
void stack_dragons (void)
{
    uint8_t from_x;
    uint8_t from_y;
    uint8_t to_x;
    uint8_t to_y;

    uint8_t dragon_idx = cursor_depth;
    uint8_t card_match = 0x30 + dragon_idx;
    uint8_t dest_idx = 0xff;

    /* First, see if we already have a slot */
    for (uint8_t stack_idx = CURSOR_DRAGON_SLOT_1; stack_idx <= CURSOR_DRAGON_SLOT_3; stack_idx++)
    {
        if (stack [stack_idx] [0] == card_match)
        {
            dest_idx = stack_idx;
            break;
        }
    }

    /* If not, choose the first empty slot */
    if (dest_idx == 0xff)
    {
        for (uint8_t stack_idx = CURSOR_DRAGON_SLOT_1; stack_idx <= CURSOR_DRAGON_SLOT_3; stack_idx++)
        {
            if (stack [stack_idx] [0] == 0xff)
            {
                dest_idx = stack_idx;
                break;
            }
        }
    }

    /* Animation end-point */
    cursor_sd_to_xy (dest_idx, 0, &to_x, &to_y);
    stack [STACK_HELD] [0] = card_match;
    stack [STACK_HELD] [1] = 0xff;

    /* Remove the dragons from wherever they may be */
    for (uint8_t stack_idx = 0; stack_idx <= CURSOR_DRAGON_SLOT_3; stack_idx++)
    {
        uint8_t top = top_card (stack_idx);

        if (stack [stack_idx] [top] == card_match)
        {
            /* Animation start-point */
            cursor_sd_to_xy (stack_idx, top, &from_x, &from_y);

            stack [stack_idx] [top] = 0xff;
            stack_changed [stack_idx] = true;
            render_background ();

            card_slide (from_x, from_y, to_x, to_y, 10, false);

            /* If there are currently no cards in the destination, draw one after the first slide-animation */
            if (stack [dest_idx] [0] == 0xff)
            {
                stack [dest_idx] [0] = card_match;
                stack [dest_idx] [1] = 0xff;
                stack_changed [dest_idx] = true;
                render_background ();
            }
        }
    }

    stack [STACK_HELD] [0] = 0xff;

    /* Place all four dragons into the destination slot */
    for (uint8_t i = 0; i < 4; i++)
    {
        stack [dest_idx] [i] = card_match;
    }
    stack [dest_idx] [4] = 0xff;
}


/*
 * Move the currently-selected card to the foundation if possible.
 */
void move_auto (void)
{
    uint8_t from_x;
    uint8_t from_y;
    uint8_t to_x;
    uint8_t to_y;

    uint8_t from_stack = cursor_stack;

    /* If we're already pointing at a foundation, there is nothing to do */
    if (from_stack >= CURSOR_FOUNDATION_SNEP)
    {
        return;
    }

    /* Only the top card in a stack can be auto-moved */
    if (cursor_depth != top_card (from_stack))
    {
        return;
    }

    /* Animation start-point */
    cursor_sd_to_xy (from_stack, cursor_depth, &from_x, &from_y);

    cursor_pick ();

    /* Abort if there wasn't actually a card */
    if (stack [STACK_HELD] [0] == 0xff)
    {
        return;
    }

    /* Try placing in each slot */
    for (uint8_t i = CURSOR_FOUNDATION_SNEP; i <= CURSOR_FOUNDATION_3; i++)
    {
        cursor_stack = i;
        cursor_depth = CURSOR_DEPTH_MAX;
        cursor_move (PORT_A_KEY_DOWN);
        cursor_place ();

        /* Placement was successful */
        if (stack [STACK_HELD] [0] == 0xff)
        {
            /* Animation end-point */
            cursor_sd_to_xy (cursor_stack, cursor_depth, &to_x, &to_y);

            cursor_pick ();
            render_background ();
            card_slide (from_x, from_y, to_x, to_y, 10, false);
            cursor_place ();
            render_background ();
            break;
        }
    }

    /* Restore cursor position, returning the card if we still have it */
    cursor_stack = from_stack;
    cursor_depth = CURSOR_DEPTH_MAX;
    cursor_move (PORT_A_KEY_DOWN);
    if (stack [STACK_HELD] [0] != 0xff)
    {
        cursor_place ();
    }
}


/*
 * Cancel the current card movement, returning to where it came from.
 */
void move_cancel (void)
{
    uint8_t from_x;
    uint8_t from_y;
    uint8_t to_x;
    uint8_t to_y;

    /* Animation start-point */
    cursor_sd_to_xy (cursor_stack, cursor_depth, &from_x, &from_y);

    cursor_stack = came_from;
    cursor_depth = CURSOR_DEPTH_MAX;
    cursor_move (PORT_A_KEY_DOWN);

    /* Animation end-point*/
    cursor_sd_to_xy (cursor_stack, cursor_depth + 1, &to_x, &to_y);

    card_slide (from_x + 2, from_y + 12, to_x, to_y, 10, true);
    cursor_place ();

    /* Update the background early, to avoid a frame without the card */
    render_background ();
}


/*
 * Play one game.
 */
void game (void)
{
    bool playing = true;

    while (playing)
    {
        static uint16_t keys_previous = 0;
        uint16_t keys = SMS_getKeysStatus ();
        uint16_t keys_pressed = (keys & ~keys_previous);

        /* Logic */
        if (keys_pressed & PORT_A_KEY_DPAD)
        {
            cursor_move (keys_pressed);
        }

        if (keys_pressed & PORT_A_KEY_1)
        {
            if (stack [STACK_HELD] [0] == 0xff)
            {
                if (cursor_stack == CURSOR_DRAGON_BUTTONS)
                {
                    if (button_active [cursor_depth])
                    {
                        stack_dragons ();
                    }
                }
                else
                {
                    cursor_pick ();
                }
            }
            else
            {
                cursor_place ();
            }
        }
        else if (keys_pressed & PORT_A_KEY_2)
        {
            if (cursor_stack == CURSOR_DRAGON_BUTTONS)
            {
                /* Do nothing */
            }
            else if (stack [STACK_HELD] [0] == 0xff)
            {
                move_auto ();
            }
            else
            {
                move_cancel ();
            }
        }

        keys_previous = keys;

        /* Update H/W during vblank */
        SMS_waitForVBlank ();

        if (sprite_update)
        {
            SMS_copySpritestoSAT ();
            sprite_update = false;
        }
        render_background ();
manageScroll();


        /* Check if the game is still in progress */
        playing = false;
        for (uint8_t i = 0; i <= CURSOR_COLUMN_8; i++)
        {
            if (stack [i] [0] != 0xff)
            {
                playing = true;
            }
        }
        if (stack [STACK_HELD] [0] != 0xff)
        {
            playing = true;
        }
    }
}


/*
 * Cycle through different colour schemes.
 */
void next_palette (void)
{
    static uint8_t index = 0;

    index = (index + 1) % 3;

    switch (index)
    {
        case 0:
            GG_setSpritePaletteColor (0, RGB(33,10,77)); 
            GG_setBGPaletteColor     (0, RGB(33,10,77));      //the board color
            GG_setBGPaletteColor     (1, RGB(0x00,0x00,0x00)); /* Light green */      //the card outline
            break;
        case 1:
            GG_setSpritePaletteColor (0, 0x2a);  //0x24   0xe8
            GG_setBGPaletteColor     (0, 0x2a); 
            GG_setBGPaletteColor     (1, RGB(0x00,0x00,0x00)); /* Dark blue */
            break;
        case 2:
            GG_setSpritePaletteColor (0, 0x33); //0x33
            GG_setBGPaletteColor     (0, 0x33); 
            GG_setBGPaletteColor     (1, RGB(0x00,0x00,0x00)); /* Brick*/
            break;
    }
}

void manageScroll()
{
//scrollX=(cursor_stack-6)*8;
//if(scrollX>48)scrollX=48;

if(cursor_stack==CURSOR_COLUMN_1  || cursor_stack==CURSOR_DRAGON_SLOT_1)scrollX=48;//48
else if(cursor_stack==CURSOR_COLUMN_2 || cursor_stack==CURSOR_DRAGON_SLOT_2)scrollX=38;//40
else if(cursor_stack==CURSOR_COLUMN_3 || cursor_stack==CURSOR_DRAGON_SLOT_3)scrollX=28;//32
else if(cursor_stack==CURSOR_COLUMN_4 || cursor_stack==CURSOR_DRAGON_BUTTONS)scrollX=18;//24
else if(cursor_stack==CURSOR_COLUMN_5 || cursor_stack==CURSOR_FOUNDATION_SNEP)scrollX=8;//16
else if(cursor_stack==CURSOR_COLUMN_6 || cursor_stack==CURSOR_FOUNDATION_1)scrollX=238;//8
else if(cursor_stack==CURSOR_COLUMN_7 || cursor_stack==CURSOR_FOUNDATION_2)scrollX=228;//0
else if(cursor_stack==CURSOR_COLUMN_8 || cursor_stack==CURSOR_FOUNDATION_3)scrollX=208;//216
//else scrollX=0;

SMS_setBGScrollX(scrollX);
}

/*
 * Main menu.
 */
void menu (void)
{

    uint16_t card_tiles [] = {
        BLANK_CARD +  0, BLANK_CARD +  2, BLANK_CARD +  2, BLANK_CARD +  3,
        //BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        //BLANK_CARD +  5, BLANK_CARD +  6, BLANK_CARD +  6, BLANK_CARD +  7,
        BLANK_CARD +  8, BLANK_CARD +  9, BLANK_CARD +  9, BLANK_CARD + 10
    };

    /* Render background */
    memset (stack_changed, true, sizeof (stack_changed));
    render_background ();
    cursor_stack = 2;
    cursor_depth = 0;
    cursor_render ();

    /* Render menu cards */
    for (uint8_t i = 0; i < 3; i++)
    {
        uint16_t tile = MENU_TEXT + (4 * i);
        card_tiles [4] = tile;//4
        card_tiles [5] = tile + 1;//5
        card_tiles [6] = tile + 2;//6
        card_tiles [7] = tile + 3;//7

        tile = MENU_ICONS + (4 * i);
        card_tiles [9] = tile;//13
        card_tiles [10] = tile + 1;//14
        card_tiles [13] = tile + 2;//17
        card_tiles [14] = tile + 3;//18
//SMS_loadTileMapArea (unsigned char x, unsigned char y,  unsigned int *src, unsigned char width, unsigned char height);
        SMS_loadTileMapArea ((4 * (i + 2)), 10, &card_tiles, 4, 4);// last parameter height 6->4  //2nd parameter Y 9->8
    }

    while (in_menu)
    {

        static uint16_t keys_previous = 0;
        uint16_t keys = SMS_getKeysStatus ();
        uint16_t keys_pressed = (keys & ~keys_previous);

        /* Logic */
        if (keys_pressed & PORT_A_KEY_DPAD)
        {
            cursor_move (keys_pressed);
        }


        if (keys_pressed & PORT_A_KEY_1)
        {
            /* Start */
            if (cursor_stack == 2)
            {
                in_menu = false;
            }
            /* Table */
            else if (cursor_stack == 3)
            {
                next_palette ();
            }
            /* Arrow */
            else if (cursor_stack == 4)
            {
                cursor_style = (cursor_style + 1) % 3;
                cursor_render ();
            }
        }

        keys_previous = keys;

        SMS_waitForVBlank ();

        if (sprite_update)
        {
            SMS_copySpritestoSAT ();
            sprite_update = false;
        }
    }

    memset (stack_changed, true, sizeof (stack_changed));
    render_background ();
manageScroll();

}


/*
 * Entry point.
 */

/*
const uint8_t palette [16] = {
    0x94,   //  0 - (table) Dark green 
    0x19,   // 1 - (table) Light green 
    0x00,   //  2 - (card / cursor) Black 
    0x3f,   //  3 - (card / cursor) White 
    0xa9,//0x02,   //  4 - (card) Red 
    0x09,//0x09,   //  5 - (card / menu) Green 
    0xd9,   //  6 - (cursor) Light grey       //fill the card color
    0xb1,//0x15,   //  7 - (cursor) Dark grey   //outline the card
    0xc4,   //  8 - (menu) Blue 
    0x16,   //  9 - (menu) Brick 
    0x04,   // 10 - (menu) Dark green 
*/

void main (void)
{
    /* Setup */
    GG_loadBGPalette (palette);
    GG_loadSpritePalette (palette);
    SMS_setBackdropColor (0);

//begin new palette stuff
GG_setBGPaletteColor (1, RGB(0x01,0x00,0x00)); //card outline

GG_setBGPaletteColor (3, RGB(0xff,0xff,0xff));  //makes the cards inside fill white - good

GG_setBGPaletteColor (0, RGB(20,0xd1,20));//background

GG_setSpritePaletteColor (3, RGB(0x00,0xff,20));
//end new palette stuff

    SMS_loadTiles (patterns, 0, sizeof (patterns));
    clear_background ();

    SMS_useFirstHalfTilesforSprites (true);
    SMS_initSprites ();
    SMS_copySpritestoSAT ();

    SMS_displayOn ();

    menu ();

    /* Main loop */
    while (true)
    {
        deal ();
        game ();
        undeal ();
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);