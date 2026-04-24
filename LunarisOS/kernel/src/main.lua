packed struct limine_base_revision
    magic0: u64
    magic1: u64
    revision: u64
end

packed struct limine_framebuffer
    address: ptr(u8)
    width: u64
    height: u64
    pitch: u64
end

packed struct limine_framebuffer_response
    revision: u64
    framebuffer_count: u64
    framebuffers: ptr(ptr(limine_framebuffer))
end

packed struct limine_framebuffer_request
    id0: u64
    id1: u64
    id2: u64
    id3: u64
    revision: u64
    response: ptr(limine_framebuffer_response)
end

packed struct limine_requests_start_marker
    q0: u64
    q1: u64
    q2: u64
    q3: u64
end

packed struct limine_requests_end_marker
    q0: u64
    q1: u64
end

data limine_requests_start_marker: limine_requests_start_marker section ".limine_requests_start" = 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, 0x785c6ed015d3e316, 0x181e920a7852b9d9
data limine_base_revision: limine_base_revision section ".limine_requests" = 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 6
data limine_framebuffer_request: limine_framebuffer_request section ".limine_requests" = 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x9d5827dcd881dd75, 0xa3148604f6fab11b, 0, 0
data limine_requests_end_marker: limine_requests_end_marker section ".limine_requests_end" = 0xadc0e0531bb10d03, 0x9572709f31764c62

asm function hlt(): void = hlt;
require "terminal"

function hcf()
    while 1 do
        hlt()
    end
end

function hcf_base_revision()
    while 1 do
        hlt()
    end
end

function hcf_framebuffer_response()
    while 1 do
        hlt()
    end
end

function hcf_framebuffer_count()
    while 1 do
        hlt()
    end
end

function kernel_main()
    local base_revision: ptr(limine_base_revision) = limine_base_revision
    if base_revision.revision ~= 0 then
        hcf_base_revision()
    end

    local framebuffer_request: ptr(limine_framebuffer_request) = limine_framebuffer_request
    if framebuffer_request.response == 0 then
        hcf_framebuffer_response()
    end

    if framebuffer_request.response.framebuffer_count < 1 then
        hcf_framebuffer_count()
    end

    local framebuffer: ptr(limine_framebuffer) = framebuffer_request.response.framebuffers[0]
    local fb_ptr: ptr(u32) = framebuffer.address
    local pitch: u64 = framebuffer.pitch / 4
    terminal_init(fb_ptr, framebuffer.width, framebuffer.height, pitch)

    print("\x1b[38;2;255;208;120mLunaris framebuffer terminal\x1b[0m")
    printf("framebuffer %u x %u pitch %u\n", framebuffer.width, framebuffer.height, framebuffer.pitch, 0, 0)
    printf("\x1b[38;2;120;200;255mcursor demo\x1b[0m: %u,%u\n", 0, 0, 0, 0, 0)

    hcf()
end