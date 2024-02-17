#include "transfer.h"
#include "hardware.h"
#include "graphics.h"
#include "start.h"

// This will ensure code is executable from RAM
#include "area_ram.h"

void wait_for_vblank_or_recv_byte() {
    bool done;
    while (!(done = *rLY == 144));
}

void show_ram_is_working() {
    range_t* range = ((range_t*)(&tiles + pb_8_tile_index));
    wait_for_vblank_or_recv_byte();
    set_tiles_row_repeat(14, 0, *range, 1);
}

// typedef struct {
//     uint8_t mode;
// } header_t;

void send_message(uint8_t* data, uint8_t len){
}

void send_byte(uint8_t byte, uint8_t use_internal_clock){
    *rSB = byte;
    *_VRAM = byte;
    *rSC = LINK_CABLE_ENABLE | use_internal_clock;
}

uint8_t recv_byte(uint8_t timeout){
    for(uint16_t i = 0; i < (((uint16_t)timeout) << 8) || ((*rSC) & 0x80) != 0; ++i);
    return *rSB;
}

void wait_for_other_device(uint8_t use_internal_clock) {
    uint8_t packet_to_send = use_internal_clock? ~LINK_CABLE_MAGIC_PACKET_SYNC : LINK_CABLE_MAGIC_PACKET_SYNC;
    uint8_t packet_to_receive = ~packet_to_send;
retry:
    send_byte(packet_to_send, use_internal_clock);
    uint8_t received_packet = recv_byte(0);
    if(received_packet != packet_to_receive){
        goto retry;
    }
}

void send_recv_header(uint8_t use_internal_clock){
    send_byte(*rDevice_mode, use_internal_clock);
    *rDevice_mode_remote = recv_byte(0);
    send_byte(*rTransfer_mode, use_internal_clock);
    *rTransfer_mode_remote = recv_byte(0);
}

void ram_fn_transfer_header(void) {
    uint8_t use_internal_clock = *rRole == ROLE_LEADER;
    wait_for_other_device(use_internal_clock);
    send_recv_header(use_internal_clock);
    show_ram_is_working();
}