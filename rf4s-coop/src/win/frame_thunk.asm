; MSVC version of the frame-hook thunk; see frame_hook.cpp for what it does.
; Keep it instruction-for-instruction the same as the GNU version there.

EXTERN rf4s_coop_frame_callback:PROC
EXTERN rf4s_coop_frame_original:QWORD

.code
rf4s_coop_frame_thunk PROC
    push rcx
    push rdx
    push r8
    push r9
    sub rsp, 68h
    movdqu xmmword ptr [rsp+20h], xmm0
    movdqu xmmword ptr [rsp+30h], xmm1
    movdqu xmmword ptr [rsp+40h], xmm2
    movdqu xmmword ptr [rsp+50h], xmm3
    call rf4s_coop_frame_callback
    movdqu xmm0, xmmword ptr [rsp+20h]
    movdqu xmm1, xmmword ptr [rsp+30h]
    movdqu xmm2, xmmword ptr [rsp+40h]
    movdqu xmm3, xmmword ptr [rsp+50h]
    add rsp, 68h
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp qword ptr [rf4s_coop_frame_original]
rf4s_coop_frame_thunk ENDP

END
