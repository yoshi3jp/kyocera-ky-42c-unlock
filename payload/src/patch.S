.syntax unified
.thumb

.section .text.main, "ax"
.global main
.type main, %function

main:

    /* Set g_da_verified to 1 to bypass jump_da verification */
    movw r0, #0x9609
    movt r0, #0x0010
    mov  r1, #1
    strb r1, [r0]

    /* Patch SBC check in send_da to always pass verification */
    movw r0, #0x9ab4
    movt r0, #0x0022
    movw r1, #0x2000 ; movs r0, #0
    strh r1, [r0]
    movw r0, #0x9ab6
    movt r0, #0x0022
    movw r1, #0x4770 ; bx lr
    strh r1, [r0]

    /* dcache and icache */
    movw r0, #0x9ab4
    movt r0, #0x0022
    mcr  p15, 0, r0, c7, c10, 1

    mov  r0, #0
    mcr  p15, 0, r0, c7, c5, 0
    mcr  p15, 0, r0, c7, c5, 6
    dsb  sy
    isb  sy


    /* Branch back to preloader, after the `bldr_jump` call
     * so that we can load an unsigned DA
     */
    movw lr, #0x2d67
    movt lr, #0x0020
    bx   lr
