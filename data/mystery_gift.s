#include "constants/global.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/map_scripts.h"
#include "constants/mystery_gift.h"
#include "constants/moves.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "constants/vars.h"
#include "constants/wild_encounter.h"
	.include "asm/macros.inc"
	.include "asm/macros/event.inc"
	.include "constants/constants.inc"

	.section .rodata

	.align 2
	.include "data/scripts/gift_stamp_card.inc"
	.include "data/scripts/gift_pichu.inc"
	.include "data/scripts/gift_trainer.inc"
	.include "data/scripts/gift_aurora_ticket.inc"
	.include "data/scripts/gift_mystic_ticket.inc"
	.include "data/scripts/gift_altering_cave.inc"
