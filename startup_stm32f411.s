.syntax unified
.cpu cortex-m4
.thumb

.global _estack
.global Reset_Handler

.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object

g_pfnVectors:
    .word _estack
    .word Reset_Handler

.section .text.Reset_Handler
Reset_Handler:
    bl main

loop:
    b loop
