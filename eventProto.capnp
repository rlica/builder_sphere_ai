@0x86067e6a0fae5e5e;

struct Dataset {
    events @0: List(Event);

    struct Event {
        board @0 :UInt8;
        channel @1 :UInt8;
        energy @2: UInt16;
        timestamp @3: UInt64;
        psd @4: Float32;
        waveform @5: List(Int16);
    }
}

