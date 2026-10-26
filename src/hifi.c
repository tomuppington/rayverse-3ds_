
//3E66C
void sub_3E66C(void) {
    print_once("Not implemented: sub_3E66C"); //stub
}

//3E6B0
void sub_3E6B0(void) {
    print_once("Not implemented: sub_3E6B0"); //stub
}

//3E6EC
void rayman_sound_fatal_error(const char* message) {
    printf("Rayman sound says fatal error :\n%s\n", message);
    exit(-1);
}

//3E710
void sub_3E710(void) {
    print_once("Not implemented: sub_3E710"); //stub
}

//3E780
void sub_3E780(void) {
    print_once("Not implemented: sub_3E780"); //stub
}

//3E820
void LoadBnkFile(s32 header_index, s32 data_index, bnk_header_t* headers, u8** data) {
    lock_audio(); // added

    // Load sound header
    FILE* fp = open_data_file("SNDH8B.DAT", false);
    if (!fp) {
        rayman_sound_fatal_error("Fichier data introuvable.\n");
    }

    archive_header_t* header_info = snd8b_header_infos + header_index;
    fseek(fp, header_info->offset, SEEK_SET);
    fread(headers, 1, header_info->size, fp); // always 0x800 bytes
    fclose(fp);

    u8 checksum = decode_xor((u8*)headers, header_info->size, header_info->xor_byte, header_info->checksum_byte);
    if (checksum != 0) {
        rayman_sound_fatal_error("Load error in sndh8b.dat.\n");
    }

    stop_cd();
    fp = open_data_file("SNDD8B.DAT", false);
    if (!fp) {
        rayman_sound_fatal_error("Fichier data introuvable.\n");
    }
    header_info = snd8b_data_infos + header_index;
    if (*data != NULL) {
        free(*data);
    }
    *data = (u8*)malloc(header_info->size);
    if (!*data) {
        rayman_sound_fatal_error("Mmoire insuffisante pour charger les samples.\n");
    }
    fseek(fp, header_info->offset, SEEK_SET);
    fread(*data, 1, header_info->size, fp);
    fclose(fp);
    checksum = decode_xor(*data, header_info->size, header_info->xor_byte, header_info->checksum_byte);
    if (checksum != 0) {
        rayman_sound_fatal_error("Load error in sndd8b.dat.\n");
    }

    // NOTE: here, apparently the game replaces the offsets in the header with pointers to live data
    // We skip this because it obviously only works on 32-bit systems (for now; might need to change the code later).
    // We should keep this difference in mind when we want to access the sound data.
#if 0
    for (s32 i = 1; i < 128; ++i) {
        bnk_header_t* bnk_header = headers + i;
        bnk_header[i].offset += (s32)*data;
    }
#endif

    unlock_audio(); // added
}

void LoadBnkFile_debug(s32 sound_set, s32 a2, u8** sound_buffer) {
    // Load sound header
    mem_t* mem = read_entire_file("SNDH8B.DAT", true);
    if (!mem) fatal_error();

    archive_header_t* header_info = &snd8b_header_infos[sound_set];

    mem_seek(mem, header_info->offset);
    mem_read(bnkHeaderFixe, mem, header_info->size); // always 0x800 bytes

    u8 checksum = decode_xor((u8*)bnkHeaderFixe, header_info->size, header_info->xor_byte, header_info->checksum_byte);
    if (checksum != 0) {
        printf("[warning] LoadBnkFile(): incorrect checksum for SNDH8B.DAT\n");
    }

    free(mem);

    // Load sound data
    archive_header_t* data_info = &snd8b_data_infos[sound_set];
    size_t sound_size = data_info->size;
    void* temp = realloc(*sound_buffer, sound_size);
    if (!temp) fatal_error();
    *sound_buffer = (u8*)temp;

    FILE* fp = open_data_file("SNDD8B.DAT", true);
    if (!fp) fatal_error();

    fseek(fp, data_info->offset, SEEK_SET);
    size_t bytes_read = fread(*sound_buffer, 1, sound_size, fp);
    if (bytes_read != data_info->size) fatal_error();

    fclose(fp);

    checksum = decode_xor(*sound_buffer, (u32)sound_size, data_info->xor_byte, data_info->checksum_byte);
    if (checksum != 0) {
        printf("[warning] LoadBnkFile(): incorrect checksum for SNDD8B.DAT\n");
    }


}

//3E9B4
void LoadBnkFixe(void) {
    if (CarteSonAutorisee && !SonLimite) {
        LoadTchatchPerdu();
    }
    LoadBnkFile(2, 2, bnkHeaderFixe, &bnkDataFixe);
    ps1_sfx_load_fixed(); // 3DS port: PS1 sound effects, if there (ps1_sfx.c)
}

//3EA00
void LoadBnkWorld(s16 world) {
    switch(world) {
        case 1: {
            LoadBnkFile(4, 4, bnkHeaderWorld, &bnkDataWorld);
        } break;
        case 2: {
            LoadBnkFile(6, 6, bnkHeaderWorld, &bnkDataWorld);
        } break;
        case 3: {
            LoadBnkFile(5, 5, bnkHeaderWorld, &bnkDataWorld);
        } break;
        case 4: {
            LoadBnkFile(3, 3, bnkHeaderWorld, &bnkDataWorld);
        } break;
        case 5: {
            LoadBnkFile(1, 1, bnkHeaderWorld, &bnkDataWorld);
        } break;
        case 6: {
            LoadBnkFile(0, 0, bnkHeaderWorld, &bnkDataWorld);
        } break;
        default: break;
    }
    ps1_sfx_load_world(world); // 3DS port: PS1 sound effects, if there (ps1_sfx.c)
}

// Set by the caller just before KeyOn(): whether the sample may use its loop points. The sound banks
// give loop points for some samples that the game also plays as one-shots (e.g. a bug's buzz), so only
// the sounds the game treats as continuous (flag 0x10 in sound_table, which it then manages and stops,
// see manage_snd) loop. Reset after every KeyOn.
bool keyon_loop_allowed;

//3EABC
s16 KeyOn(u8 bank, u8 prog, u8 tone, u8 note, u8 volume, u8 pan) {
//    printf("KeyOn: bank %d prog %d tone %d note %d volume %d pan %d\n", bank, prog, tone, note, volume, pan);
    bool loop_allowed = keyon_loop_allowed;
    keyon_loop_allowed = false;
    snd_t snd = {0};
    s32 sample_rate = 11025;
    float base_note = 54.5f; // NOTE: I determined this value by trial and error, it may not be exactly correct
    if (note != 60) {
        sample_rate = (s32)((float)sample_rate * powf(2.0f, ((float)note - base_note) / 12.0f));
    }

    // Fix (3DS port): a KeyOn with volume 0 used to start a silent voice and take over the voice-table
    // slot of the sound already playing for that object, orphaning it. PlaySnd() requests volume 0
    // when the same sound is already playing (e.g. it is called repeatedly while Rayman charges his
    // fist); with looping sounds the orphaned voice would never be stopped. Starting nothing instead
    // keeps the original voice tracked, and is otherwise inaudible either way.
    if (volume == 0) {
        keyon_snd = -1;
        return -1;
    }
    // 3DS port: the PS1 recording, if there is one (ps1_sfx.c).
    if (ps1_sfx_setup(&snd, bank, prog, tone, note, volume, pan, loop_allowed)) {
        keyon_snd = -1;
        s16 voice_index = play_digi_snd(&snd);
        if (voice_index >= 0 && voice_index < COUNT(voice_table)) {
            voice_table[voice_index].obj = -2;
            voice_table[voice_index].snd = -1;
            voice_is_working[voice_index] = 0;
        }
        return voice_index;
    }
    keyon_snd = -1;
    bnk_header_t* loop_header = (bank == 1) ? bnkHeaderWorld + prog : bnkHeaderFixe + prog;
    // field_8 / field_C of the bank header are the loop start and the loop length (0, 0 for one-shot
    // samples). (Almost all loops start at 0, where length and end are the same; the one that doesn't, the
    // whoosh of the flying picks and other flying things, loops the whole rest of the sample, as on the
    // PS1 - read as an end, it looped a tiny piece over and over, which buzzed.)
    if (loop_allowed && loop_header->field_C > 0 && loop_header->field_8 + loop_header->field_C <= loop_header->size) {
        snd.loop_start = loop_header->field_8;
        snd.loop_end = loop_header->field_8 + loop_header->field_C;
    }
    if (bank == 1) {
        bnk_header_t* bnk_header = bnkHeaderWorld + prog;
        snd.data = bnkDataWorld + bnk_header->offset;
        snd.offset = bnk_header->offset;
        snd.size = bnk_header->size;
        snd.bytes_per_sample = 1;
        snd.sample_count = snd.size;
        snd.bnk_field_C = bnk_header->field_C;
        snd.sample_rate = sample_rate;
        snd.position = 0.0;
        snd.volume = MIN(1.0f, (float)volume / 50.0f);
    } else {
        bnk_header_t* bnk_header = bnkHeaderFixe + prog;
        snd.data = bnkDataFixe + bnk_header->offset;
        snd.offset = bnk_header->offset;
        snd.size = bnk_header->size;
        snd.sample_count = snd.size;
        snd.sample_rate = sample_rate;
        snd.bytes_per_sample = 1;
        snd.position = 0.0;
        snd.volume = MIN(1.0f, (float)volume / 50.0f);
    }
    s16 voice_index = play_digi_snd(&snd);
    if (voice_index >= 0 && voice_index < COUNT(voice_table)) {
        // This channel now plays a new sound: forget what the voice table said about its previous one
        // (the caller fills it in again if it tracks this sound), so manage_snd() can't stop it by mistake.
        voice_table[voice_index].obj = -2;
        voice_table[voice_index].snd = -1;
        voice_is_working[voice_index] = 0;
    }
    return voice_index;
}

//3EC0C
void KeyOff(u32 voice_id, u8 bank, u8 prog, u8 tone, u8 note) {
    if (voice_id >= COUNT(digi_voices)) return; // added: guard against invalid voice ids
    snd_t* digi_voice = digi_voices + voice_id;
    if (digi_voice->ps1_pan && digi_voice->is_playing) {
        // PS1 sound: fades out quickly (as the PS1 sound chip releases a note) rather than cutting off.
        if (digi_voice->release_left == 0) digi_voice->release_left = 882; // 20 ms
        return;
    }
    digi_voice->is_playing = false;
}

//3EC1C
void KeyVol(s32 voice_id, s32 volume, s32 pan) {
    if (voice_id < 0 || voice_id >= COUNT(digi_voices)) return; // added: guard against invalid voice ids
    snd_t* digi_voice = digi_voices + voice_id;
    if (digi_voice->ps1_pan) {
        digi_voice->volume = (float)volume / 127.0f * digi_voice->ps1_gain;
        digi_voice->pan_l = (float)snd_sqrt_table[127 - (pan & 127)] / 256.0f;
        digi_voice->pan_r = (float)snd_sqrt_table[pan & 127] / 256.0f;
        return;
    }
    digi_voice->volume = MIN(1.0f, (float)volume / 50.0f);
}

//3EC78
void DigiMusicDone(void) {
    for (s32 i = 0; i < COUNT(voice_table); ++i) {
        KeyOff(i, 0, 0, 0, 0);
    }
    print_once("Not implemented: DigiMusicDone"); //stub
}

//3ECC4
void LoadTchatchPerdu(void) {
    stop_cd();
    FILE* fp = open_data_file("SNDVIG.DAT", true);
    if (fp) {
        archive_header_t* sndvig_header = sndvig_infos + 6;
        ptrTchatch = (u8*)malloc(sndvig_header->size);
        fseek(fp, sndvig_header->offset, SEEK_SET);
        fread(ptrTchatch, 1, sndvig_header->size, fp);
        fclose(fp);
        u8 checksum = decode_xor(ptrTchatch, sndvig_header->size, sndvig_header->xor_byte, sndvig_header->checksum_byte);
        if (checksum != 0) {
            rayman_sound_fatal_error("Load error in sndvig.dat.\n");
        }
        TchachPerduPtr = ptrTchatch;
        TchachPerduSize = sndvig_header->size;
        ptrTchatch = NULL;
    }
}

//3EDF4
static s16 tchatch_voice = -1;
static u8* tchatch_voice_data;

void PlayTchatch(s32 snd, s32 volume) {
    // Implemented for the 3DS port (was a stub). Plays a voice clip from SNDVIG.DAT: 16-bit mono at
    // 11025 Hz. The original mapping from 'snd' to clip isn't documented, so it was reconstructed by
    // listening to the clips and matching them to where the game asks for them:
    //   1 Jungle (Tarayzan): "Hey Rayman! Take this magic seed!"          -> clip 8
    //   2 Mountain (Musician): "Thanks Rayman baby, super helicopter..." -> clip 5
    //   3 Cave level 3 (Joe): gibberish                                  -> clip 2
    //   4 Image City: "Uh oh, Mr Dark kidnaps Betilla the Fairy!"        -> clip 0
    //   6 perfect bonus: "Perfect!"                                      -> clip 7
    //   7 Candy Chateau (Mr Dark): "You're doomed Rayman, hahaha"         -> clip 1
    //   8 death                                                          -> clip 6 (preloaded by LoadTchatchPerdu)
    static const s8 clip_for_snd[9] = { -1, 8, 5, 2, 0, -1, 7, 1, 6 };
    FreeTchatchVignette();
    if (snd < 0 || snd >= (s32)COUNT(clip_for_snd) || clip_for_snd[snd] < 0) return;
    s32 clip = clip_for_snd[snd];

    u8* data = NULL;
    s32 size = 0;
    if (clip == 6 && TchachPerduPtr) {
        data = TchachPerduPtr;
        size = TchachPerduSize;
    } else {
        FILE* fp = open_data_file("SNDVIG.DAT", false);
        if (!fp) return;
        archive_header_t* info = sndvig_infos + clip;
        data = (u8*)malloc(info->size);
        if (!data) {
            fclose(fp);
            return;
        }
        fseek(fp, info->offset, SEEK_SET);
        fread(data, 1, info->size, fp);
        fclose(fp);
        if (decode_xor(data, info->size, info->xor_byte, info->checksum_byte) != 0) {
            free(data);
            return;
        }
        size = info->size;
        ptrTchatch = data; // freed by FreeTchatchVignette()
    }

    snd_t voice = {0};
    voice.data = data;
    voice.size = size;
    voice.sample_count = size / 2;
    voice.bytes_per_sample = 2;
    voice.sample_rate = 11025;
    voice.volume = (volume >= 20) ? 1.0f : (float)MAX(volume, 0) / 20.0f; // sound volume option is 0..20
    tchatch_voice = play_digi_snd(&voice);
    tchatch_voice_data = data;
}

//3EFA4
void FreeTchatchVignette(void) {
    // Implemented for the 3DS port (was a stub): stop the voice clip and free its buffer.
    if (tchatch_voice >= 0) {
        if (digi_voices[tchatch_voice].data == tchatch_voice_data) { // not reused by another sound since
            digi_voices[tchatch_voice].is_playing = false;
        }
        tchatch_voice = -1;
    }
    if (ptrTchatch) {
        free(ptrTchatch);
        ptrTchatch = NULL;
    }
}

//3EFD4
void SetPort_(s32 port) {
    current_port = port;
}

//3EFE0
void SetIrq(s32 irq) {
    current_irq = irq;
}

//3EFE8
void SetDma(s32 dma) {
    current_dma = dma;
}

//3EFF0
void SetParam(s32 param) {
    current_param = param;
}

//3EFF8
void SetDeviceID(s32 device_id) {
    current_device_id = device_id;
}

//3F000
char* GetDeviceName(void) {
    return current_device_name;
}

//3F008
s32 GetPort(void) {
    return current_port;
}

//3F010
s32 GetIrq(void) {
    return current_irq;
}

//3F018
s32 GetDma(void) {
    return current_dma;
}

//3F020
s32 GetParam(void) {
    return current_param;
}

//3F028
s32 GetDeviceID_Ray(void) {
    return current_device_id;
}

//3F030
void FreeDetectDriver(void) {
    print_once("Not implemented: FreeDetectDriver"); //stub
}


