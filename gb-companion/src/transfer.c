#include "transfer.h"
#include "hardware.h"
#include "graphics.h"
#include "data.h"
#include "start.h"

// This will ensure code is executable from RAM
#include "area_ram.h"

void try_update_progress_bar(uint8_t progress){
    uint8_t* dst = _SCRN1 + get_position_tile_index(3, 6) + progress / 8;
    uint8_t tile = pb_start_offset + (progress & 7);
    if (*rLY <= 144) {
        *dst = tile;
    }
}

void send_last_byte(bool use_internal_clock){
    // uint8_t fast_mode = (*rDevice_mode != DEVICE_MODE_GB) << 1;
    *rSC = LINK_CABLE_ENABLE | use_internal_clock; // | fast_mode;
}

void send_byte(uint8_t byte, bool use_internal_clock){
    *rSB = byte;
    send_last_byte(use_internal_clock);
}

uint8_t recv_byte(uint8_t timeout){
    timeout--;
    for(uint16_t i = 0; i < ((((uint16_t)timeout) << 8) | timeout) && ((*rSC) & 0x80) != 0; ++i);
    return *rSB;
}

void wait_for_other_device(bool use_internal_clock) {
    uint8_t packet_to_send = use_internal_clock? ~LINK_CABLE_MAGIC_PACKET_SYNC : LINK_CABLE_MAGIC_PACKET_SYNC;
    uint8_t packet_to_receive = ~packet_to_send;
    uint8_t received_packet;
    do {
        send_byte(packet_to_send, use_internal_clock);
        received_packet = recv_byte(0);
    } while (received_packet != packet_to_receive);
}

void send_recv_header(bool use_internal_clock){
    send_byte(*rDevice_mode, use_internal_clock);
    *rDevice_mode_remote = recv_byte(0);
    send_byte(*rTransfer_mode, use_internal_clock);
    *rTransfer_mode_remote = recv_byte(0);
    send_byte(*rCartridgeType_mode, use_internal_clock);
    *rCartridgeType_mode_remote = recv_byte(0);
    send_byte(*rCartridgeROM_size, use_internal_clock);
    *rCartridgeROM_size_remote = recv_byte(0);
    send_byte(*rCartridgeSRAM_size, use_internal_clock);
    *rCartridgeSRAM_size_remote = recv_byte(0);
}

void ram_fn_transfer_header(void) {
    bool use_internal_clock = *rRole == ROLE_LEADER;
    wait_for_other_device(use_internal_clock);
    send_recv_header(use_internal_clock);
}

#define PACKET_SIZE 128
// #define JUGGLE_SPI_MASTER

void ram_fn_perform_transfer(void) {

    uint8_t transfer_mode = *rTransfer_mode | *rTransfer_mode_remote;
    bool backup_save  = (0 != (transfer_mode & TRANSFER_MODE_BACKUP_SAVE));
    bool restore_save = (0 != (transfer_mode & TRANSFER_MODE_RESTORE_SAVE));

    bool is_leader = *rRole == ROLE_LEADER;
    bool is_receiving_data = 
        (is_leader  && backup_save) ||
        (!is_leader && restore_save);

    bool use_internal_clock = is_receiving_data;

    // TODO: grab this from MBC data
    uint8_t* data_ptr = (uint8_t*)(is_receiving_data? 0xD000 : 0xC000);

    uint8_t visual_tile_index = 0;

    // If we control the message flow, Wait some time before starting actual transfer
    // So that we know that the other end is ready for us when transfer starts.
    if (use_internal_clock){
        wait_n_cycles(0x4000);
    }

    for (uint16_t packet_num = 0; packet_num < 32; ++packet_num) {
        uint8_t checksum = 0;

        // Send packet number
        {
            send_byte(packet_num, use_internal_clock);
            uint16_t received_packet_num = recv_byte(0);
            send_byte(packet_num >> 8, use_internal_clock);
            received_packet_num |= recv_byte(0) << 8;

            // Remote wants to resend previous packet
            if (is_receiving_data){
                while (packet_num != received_packet_num){
                    packet_num--;
                    data_ptr -= PACKET_SIZE;
                }
            }
        }

        for (uint8_t i = 0; i < PACKET_SIZE; ++i, ++data_ptr){
            if(!is_receiving_data){
                uint8_t msg = *data_ptr;
                checksum = checksum ^ msg;
                send_byte(msg, use_internal_clock);
            } else {
                send_last_byte(use_internal_clock);
            }
            try_update_progress_bar(packet_num);

#ifndef JUGGLE_SPI_MASTER
            // If we are not juggling spi clock leader role back and forth,
            // wait to make sure worker is not behind and is missing packets.
            if (use_internal_clock){
                wait_n_cycles(0x120);
            }
#endif
            uint8_t received_byte = recv_byte(0);
            // TODO: On timeout, break from loop and attempt error recovery (checksum + packet nr logic)

            if (is_receiving_data){
                checksum = checksum ^ received_byte;
            }

            // Visualize transfered bytes
            *(_VRAM + tiles_end * 16 + ((visual_tile_index += 2) & 15)) = received_byte;
            *(_SCRN1 + get_position_tile_index(12, 4)) = tiles_end;

            if (is_receiving_data){
                *data_ptr = received_byte;
            }

            // This does not work in emu because of random turbo mode bug
#ifdef JUGGLE_SPI_MASTER
            use_internal_clock = !use_internal_clock;
#endif
        }

        // Exchange checksum to verify that packet was transfered correctly
        {
            send_byte(checksum, use_internal_clock);
            uint8_t received_byte = recv_byte(0);

            // If we are sending and checksum from remote is incorrect, resend last packet
            if (!is_receiving_data && received_byte != checksum){
                packet_num--;
                data_ptr -= PACKET_SIZE;
            }
        }
    }
}
