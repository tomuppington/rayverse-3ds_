
//71B80
u8 snd_sqrt_table[128] = {
        40,  46,  51,  55,  60,  64,  67,  70,  72,  74,  77,  80,  83,  86,  89,  91,
        93,  95,  96,  98,  101, 104, 107, 109, 111, 113, 115, 117, 120, 122, 124, 125,
        127, 129, 131, 133, 135, 137, 138, 140, 142, 144, 145, 147, 149, 150, 152, 153,
        155, 156, 158, 160, 161, 163, 164, 166, 167, 168, 170, 171, 173, 174, 176, 177,
        178, 180, 181, 182, 184, 185, 186, 187, 189, 190, 191, 193, 194, 195, 196, 198,
        199, 200, 201, 202, 203, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 217,
        218, 219, 220, 222, 223, 224, 225, 226, 227, 229, 230, 231, 232, 233, 234, 235,
        236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251,
};


ogg_t open_cd_vorbis(s32 track_number, bool looping) {
	ogg_t result = {0};
	if (track_number >= 2 && track_number <= 20) {
		// Changed (3DS port): stream the track from disk instead of reading the whole file (up to
		// several MB) into memory first. On the 3DS, loading a whole track from the SD card froze the
		// game for a noticeable moment every time the music changed.
		char filename[512];
		snprintf(filename, sizeof(filename), "Music/rayman%02d.ogg", track_number);
		FILE* fp = fopen(filename, "rb");
		if (!fp) {
			// Try to find a Music directory one level up (for GOG Rayman Forever version)
			snprintf(filename, sizeof(filename), "../Music/rayman%02d.ogg", track_number);
			fp = fopen(filename, "rb");
		}
		if (fp) {
			setvbuf(fp, NULL, _IOFBF, 32 * 1024); // read in 32 KB chunks (~2 seconds of music each)
			int error = 0;
			stb_vorbis* decoder = stb_vorbis_open_file(fp, 1 /* close the file when done */, &error, NULL);
			if (!decoder) {
				fclose(fp);
				return result; // TODO: failure condition
			} else {
				result.decoder = decoder;
				result.file = NULL;
				result.sample_count = stb_vorbis_stream_length_in_samples(decoder);
                result.looping = looping;
				result.samples_left = -1;
				return result;
			}
		}
	}
	return result;
}

void stop_ogg(ogg_t* ogg) {
	if (ogg->decoder) {
		stb_vorbis_close(ogg->decoder);
        ogg->decoder = NULL;
	}
	if (ogg->file) {
		free(ogg->file);
        ogg->file = NULL;
	}
}

void play_cd_track(s32 track_number, bool looping) {
	stop_ogg(&ogg_cd_track);
	ogg_cd_track = open_cd_vorbis(track_number, looping);
	is_ogg_playing = true;
	MusicCdActive = true;
}

//float volume = 0.5f;

void play_ogg(game_sound_buffer_t* sound_buffer, ogg_t* ogg) {
	// Rewritten for the 3DS port: fills the whole request even across a loop point or a change of
	// track (the old version left a gap at every loop and cleared the wrong part of the buffer), and
	// supports the PS1 soundtrack's excerpts, fade-ins and track sequences (see ps1_music.c).
	s32 samples_requested = sound_buffer->sample_count;
	s16* out = sound_buffer->samples;
	s32 done = 0;
	s32 empty_reads = 0;
	while (done < samples_requested && ogg->decoder) {
		s32 want = samples_requested - done;
		if (ogg->samples_left >= 0) want = MIN(want, ogg->samples_left);
		s32 got = 0;
		if (want > 0) {
			got = stb_vorbis_get_samples_short_interleaved(ogg->decoder, 2, out + done * 2, want * 2);
		}
		if (ogg->fade_total > 0 && ogg->fade_pos < ogg->fade_total) {
			for (s32 i = 0; i < got; ++i) {
				s32 f = MIN(ogg->fade_pos + i, ogg->fade_total);
				out[(done + i) * 2]     = (s16)((s32)out[(done + i) * 2]     * f / ogg->fade_total);
				out[(done + i) * 2 + 1] = (s16)((s32)out[(done + i) * 2 + 1] * f / ogg->fade_total);
			}
			ogg->fade_pos += got;
		}
		if (ogg->fade_out_total > 0) {
			s32 n = MIN(got, ogg->fade_out_left);
			for (s32 i = 0; i < n; ++i) {
				s32 f = ogg->fade_out_left - i;
				out[(done + i) * 2]     = (s16)((s32)out[(done + i) * 2]     * f / ogg->fade_out_total);
				out[(done + i) * 2 + 1] = (s16)((s32)out[(done + i) * 2 + 1] * f / ogg->fade_out_total);
			}
			ogg->fade_out_left -= n;
			if (ogg->fade_out_left <= 0) {
				// Faded to silence: stop the music (PS1 soundtrack, e.g. after a boss is beaten).
				done += n;
				stop_ogg(ogg);
				is_ogg_playing = false;
				break;
			}
		}
		if (ogg->samples_left >= 0) ogg->samples_left -= got;
		done += got;
		bool segment_ended = (got < want) || ogg->samples_left == 0;
		if (!segment_ended) continue;

		empty_reads = (got == 0) ? empty_reads + 1 : 0;
		if (empty_reads >= 2) break; // guard against an empty or broken file
		if (ogg->ps1) {
			if (!ps1_music_next_segment()) break; // opens the next track in ogg_cd_track (== *ogg)
		} else {
			stb_vorbis_seek_start(ogg->decoder); // loop track (rewind to start)
			SetCompteurTrameAudio();
			if (!ogg->looping) {
				is_ogg_playing = false;
				break;
			}
		}
	}
	if (done < samples_requested) {
		memset(out + done * 2, 0, (samples_requested - done) * sizeof(s16) * 2);
	}
}

float t_pitch_sine;
float t_sine;

void debug_produce_test_sound(game_sound_buffer_t* sound_buffer) {
	s16 tone_volume = 3000;
	float tone_hz = 400.0f;
	//	real32 wave_period = sound_buffer.samples_per_second/tone_hz;
	float pitch_hz = 2.0f;
	float pitch_period = sound_buffer->samples_per_second / pitch_hz;


	s16* sample_out = sound_buffer->samples;

	for (u32 sample_index = 0; sample_index < sound_buffer->sample_count; ++sample_index) {
#if 1
		t_pitch_sine += two_pi32 / pitch_period;
		if (t_pitch_sine > two_pi32) t_pitch_sine -= two_pi32;
		float pitch_sine_value = sinf(t_pitch_sine);
		float curr_hz = tone_hz + 100.0f * pitch_sine_value;
		float curr_wave_period = sound_buffer->samples_per_second / curr_hz;
		float t = t_sine += two_pi32 / curr_wave_period;
		if (t_sine > two_pi32) t_sine -= two_pi32;
		float sine_value = sinf(t);
		s16 sample_value = (s16) (sine_value * tone_volume);
#else
		s16 sample_value = 0;
#endif

		*sample_out++ = sample_value;//left
		*sample_out++ = sample_value;//right
	}
}


s16 play_digi_snd(snd_t* snd) {
    for (s32 i = 0; i < COUNT(digi_voices); ++i) {
        snd_t* voice = digi_voices + i;
        if (!voice->is_playing) {
            *voice = *snd;
            voice->position = 0.0f;
            voice->is_playing = true;
            return (s16)i;
        }
    }
    return -1;
}

void lock_audio(void) {
    print_once("Not implemented: lock_audio");
    //stub // TODO: prevent reading after sound banks have been freed (need to acquire a lock!)
}

void unlock_audio(void) {
    print_once("Not implemented: unlock_audio");
    //stub // TODO: prevent reading after sound banks have been freed (need to acquire a lock!)
}

// 3DS port: the PS1's looping sound sequences that follow an object (PS1_PlaySnd with a loop count of 0,
// then SsSepSetVol from the object's distance every frame): the Musician's guitar in the Blue Mountains
// (sound 103, sequence 22 = world bank sequence 3). The PC version has nothing there. Played from
// MusicPS1/MONGUIT.ogg (converted from the player's disc), over the music, if it is there.
typedef struct ps1_loop_snd_t {
    s16* pcm;        // the whole loop, stereo, 44100 Hz
    s32 frames;
    s32 pos;
    s16 obj;         // the object it follows
    bool playing;
    s32 vol_l, vol_r; // 0..32768
} ps1_loop_snd_t;
static ps1_loop_snd_t ps1_guitar;

static void ps1_guitar_stop(void) {
    ps1_guitar.playing = false;
}

static void ps1_guitar_start(s16 obj_id) {
    if (ps1_guitar.playing || obj_id < 0 || obj_id >= level.nb_objects) return;
    if (!ps1_guitar.pcm) {
        static bool missing;
        if (missing) return;
        int channels = 0, rate = 0;
        short* pcm = NULL;
        int frames = stb_vorbis_decode_filename("MusicPS1/MONGUIT.ogg", &channels, &rate, &pcm);
        if (frames <= 0 || channels != 2 || !pcm) {
            if (pcm) free(pcm);
            missing = true;
            return;
        }
        ps1_guitar.pcm = pcm;
        ps1_guitar.frames = frames;
    }
    ps1_guitar.obj = obj_id;
    ps1_guitar.pos = 0;
    ps1_guitar.vol_l = ps1_guitar.vol_r = 0;
    ps1_guitar.playing = true;
}

// Every frame (manage_snd): volume and stereo position from the object's distance; stops when far away
// or when the object is no longer active, as on the PS1.
static void ps1_guitar_update(void) {
    if (!ps1_guitar.playing) return;
    if (ps1_guitar.obj < 0 || ps1_guitar.obj >= level.nb_objects || !level.objects[ps1_guitar.obj].is_active) {
        ps1_guitar_stop();
        return;
    }
    obj_t* obj = level.objects + ps1_guitar.obj;
    s32 vol = get_vol_snd(obj);
    if (vol <= 5) {
        ps1_guitar_stop();
        return;
    }
    s32 pan = get_pan_snd(obj);
    s32 v = (Volume_Snd * vol) >> 7; // 0..127, as SsSepSetVol gets it
    ps1_guitar.vol_l = vol_l(v, pan) * 32768 / 127;
    ps1_guitar.vol_r = vol_r(v, pan) * 32768 / 127;
}

static void ps1_guitar_mix(game_sound_buffer_t* output_buffer) {
    if (!ps1_guitar.playing || !ps1_guitar.pcm) return;
    s16* dest = output_buffer->samples;
    for (s32 j = 0; j < output_buffer->sample_count; ++j) {
        const s16* src = ps1_guitar.pcm + ps1_guitar.pos * 2;
        s32 l = dest[0] + ((src[0] * ps1_guitar.vol_l) >> 15);
        s32 r = dest[1] + ((src[1] * ps1_guitar.vol_r) >> 15);
        dest[0] = (s16)MIN(32767, MAX(-32768, l));
        dest[1] = (s16)MIN(32767, MAX(-32768, r));
        dest += 2;
        if (++ps1_guitar.pos >= ps1_guitar.frames) ps1_guitar.pos = 0;
    }
}

void game_get_sound_samples(game_sound_buffer_t* output_buffer) {
    // Start with silence (zero everything)
    s32 bytes_per_sample = sizeof(short) * 2;
    memset(output_buffer->samples, 0, output_buffer->sample_count * bytes_per_sample);

    // Play music
	if (is_ogg_playing) {
		if (ogg_cd_track.decoder) {
			play_ogg(output_buffer, &ogg_cd_track);
		}
	}
    ps1_guitar_mix(output_buffer); // (3DS port, see above)

    // Play digi sounds
    lock_audio();

    // We need to resample the sound's sample rate (which may also vary depending on pitch shifting) to the output rate.
    const float output_sample_rate = 44100.0f;
    for (s32 i = 0; i < COUNT(digi_voices); ++i) {
        snd_t* voice = digi_voices + i;
        if (!voice->is_playing) {
            continue;
        }

        // Calculate the step increment for the source sample position.
        // This is the ratio of the sound's sample rate to the output sample rate.
        float step = (float)voice->sample_rate / output_sample_rate;

        // Get a pointer to the start of the output buffer for this frame
        s16* dest = output_buffer->samples;

        // Loop through the number of samples we need to generate for this frame
        for (s32 j = 0; j < output_buffer->sample_count; ++j) {

            // 1. Find the current position and check boundaries
            s32 source_index = (s32)voice->position;

            // Stop if we've read past the end of the source data.
            // We need source_index and source_index + 1 for interpolation, so we check against size - 1.
            if (voice->loop_end > 0 && source_index >= voice->loop_end - 1) {
                // Looping sample (e.g. Rayman's fist wind-up): jump back to the loop start. It keeps
                // playing until the game stops it with KeyOff().
                voice->position -= (float)(voice->loop_end - voice->loop_start);
                if (voice->position < 0.0f) voice->position = 0.0f;
                source_index = (s32)voice->position;
            }
            if (source_index >= voice->sample_count - 1) {
                voice->is_playing = false;
                break; // Stop processing this voice for this frame
            }

            // 2. Get samples for interpolation
            // Convert 8-bit unsigned samples to a signed range (-128 to 127)
            s32 s1, s2;
            if (voice->bytes_per_sample == 2) {
                // 16-bit signed samples (voice clips from SNDVIG.DAT), scaled to the 8-bit range used below
                const s16* data16 = (const s16*)voice->data;
                s1 = data16[source_index] / 256;
                s2 = data16[source_index + 1] / 256;
            } else {
                s1 = (s32)voice->data[source_index] - 128;
                s2 = (s32)voice->data[source_index + 1] - 128;
            }

            // 3. Perform Linear Interpolation
            // Find the fractional part of our position, which is our interpolation factor 't'
            float t = voice->position - (float)source_index;

            // Interpolate between the two samples
            float interpolated_sample = (1.0f - t) * (float)s1 + t * (float)s2;

            // 4. Scale to 16-bit and apply volume
            // Scale from the -128 to 127 range to the -32768 to 32767 range
            // Also, apply the volume for this voice
            s32 dest_sample = (s32)(interpolated_sample * 256.0f * voice->volume);

            // 5. Mix into the output buffer (mono to stereo)
            // Add the new sample to the existing data in the buffer
            s32 mixed_l = dest[0] + dest_sample;
            s32 mixed_r = dest[1] + dest_sample;

            // Clamp the values to prevent 16-bit integer overflow (clipping)
            dest[0] = (s16)MIN(32767, MAX(-32768, mixed_l));
            dest[1] = (s16)MIN(32767, MAX(-32768, mixed_r));
            dest += 2; // Move to the next stereo sample pair

            // 6. Advance our position in the source sample data
            voice->position += step;
        }
    }
    unlock_audio();
}

//71C00
void manage_snd_event(void) {
    //nullsub
}

//71C04
void SetVolumeSound(s16 volume) {
    Volume_Snd = volume;
}

//71C10
void stop_all_snd(void) {
    ps1_guitar_stop(); // (3DS port)
    if (CarteSonAutorisee) {
        for (s32 i = 0; i < COUNT(voice_table); ++i) {
            KeyOff(i, 0, 0, 0, 0);
        }
    }
}

//71C40
void stop_ray_snd(void) {
    //stub; unused in the PC version
}

//71CA8
u8 get_pan_snd(obj_t* obj) {
    s32 x = obj->offset_bx + obj->screen_x;
    s32 result;
    if (x > 416) {
        return 127;
    } else if (x >= -96) {
        result = (x + 96) >> 2;
    } else {
        result = 0;
    }
    if (result > 127)
        return 127;
    else {
        return (u8)result;
    }
}

//71CEC
s32 get_vol_snd(obj_t* obj) {
    s16 x, y, w, h;
    GET_ANIM_POS(obj, &x, &y, &w, &h);
    s16 vol = (s16)(127 - ((Abs(y + (h >> 1) - (ray.y + ray.offset_by) + 40) + Abs(x + (w >> 1) - (ray.x + ray.offset_bx))) >> 2));
    if (vol < 0) {
        return 0;
    } else {
        return vol;
    }
}

//71D80
void raj_env_sound(s16 volume) {
    SetVolumeSound((volume * 127) / 20);
}

//71DA0
void raj_env_audio(s16 enabled) {
    //nullsub
}

//71DA4
bool raj_env_stereo(s16 stereo) {
    return stereo != 0;
}

//71DA8
bool InitSnd(void) {
    if (CarteSonAutorisee) {
        for (s32 i = 0; i < 20; ++i) {
            stk_obj[i] = -2;
            stk_snd[i] = 0;
        }
        pt_pile_snd = 0;
        indice_snd_wiz = 0;
        indice_ray_wait = 0;
        indice_trz_wait = -2;
        for (s32 i = 0; i < 32; ++i) {
            voice_table[i].obj = -2;
        }
        raj_env_sound(18);
        return raj_env_stereo(options_jeu.is_stereo);
    } else {
        return false;
    }
}

//71E44
s16 last_snd(s32 obj_id) {
    s32 i = 0;
    for (; i < 20; ++i) {
        if (stk_obj[i] == obj_id) break;
    }
    if (i == 20) {
        while (i > 0) {
            --i;
            if (stk_snd[i] == 0) break;
        }
    }
    return stk_snd[i];
}

//71EA0
s32 get_pile_obj(s16 obj_id) {
    if (pt_pile_snd == 0) {
        return -1;
    }
    pile_snd_t* cur_pile_snd = pile_snd;
    s32 index = 0;
    if (pile_snd[0].obj != obj_id && pt_pile_snd > 0) {
        s16 next;
        do {
            ++cur_pile_snd;
            ++index;
        } while (pt_pile_snd > index && cur_pile_snd->obj != obj_id);
    }
    if (index == pt_pile_snd) {
        return -1;
    }

    return index;
}

//71EF4
s32 get_voice_obj(s32 obj_id) {
    voice_t* cur_voice = voice_table;
    s32 index = 0;

    if (voice_table[0].obj != obj_id) {
        do {
            ++index;
            cur_voice = voice_table + index; // fix: the pointer was never advanced
        } while (index < COUNT(voice_table) && cur_voice->obj != obj_id);
    }
    if (index == COUNT(voice_table)) {
        return -1;
    }

    return index;
}

//71F30 - unused in the PC version
s32 get_voice_snd(s32 snd) {
    voice_t* cur_voice = voice_table;
    s32 index = 0;

    if (voice_table[0].snd != snd) {
        do {
            ++index;
            cur_voice = voice_table + index; // fix: the pointer was never advanced
        } while (index < COUNT(voice_table) && cur_voice->snd != snd);
    }
    if (index == COUNT(voice_table)) {
        return -1;
    }

    return index;

}

//71F6C
s32 get_voice_obj_snd(s16 obj_id, s16 snd) {
    voice_t* voice = voice_table;
    s32 index = 0;
    // NOTE: there is a conditional here, but the branches are identical?
    // if (snd = 48) { ... } else { ... }
    while (index < COUNT(voice_table) && (voice->snd != snd || voice->obj != obj_id)) {
        ++index;
        ++voice;
    }
    if (index == COUNT(voice_table)) {
        return -1;
    }
    return index;
}

//71FC8
void erase_pile_snd(s16 obj_id) {
    s16 pile_obj_id = get_pile_obj(obj_id);
    if (pile_obj_id != -1) {
        for (s32 i = pile_obj_id; i < pt_pile_snd; ++i) {
            pile_snd[i] = pile_snd[i+1];
        }
        if (pt_pile_snd > 0) {
            --pt_pile_snd;
        }
    }
}

//720B8
void nettoie_pile_snd(void) {
    s32 index = 0;
    while (index < pt_pile_snd) {
        pile_snd_t* pile = pile_snd + index;
        if (pile->end_time < map_time && pile->end_time != 0) {
            // erase entry
            for (s32 i = index; i < pt_pile_snd; ++i) {
                pile_snd[i] = pile_snd[i+1];
            }
            if (pt_pile_snd > 0) {
                --pt_pile_snd;
            }
        } else {
            ++index;
        }
    }
}

//721C4
void erase_voice_table(s32 obj_id) {
    voice_t* cur_voice = voice_table;
    s32 result = 0;

    if (voice_table[0].obj != obj_id) {
        do {
            ++result;
            cur_voice = voice_table + result; // fix: the pointer was never advanced
        } while (result < COUNT(voice_table) && cur_voice->obj != obj_id);
    }
    if (result != COUNT(voice_table)) {
        cur_voice->obj = -2;
    }
}

//72200
u8 snd_in_pile_snd(s16 snd) {
    u8 result = false;
    s32 index = 0;
    if (pt_pile_snd != 0) {
        pile_snd_t* pile = pile_snd;
        if (pile_snd[0].snd != snd) {
            do {
                ++index;
            } while (index < pt_pile_snd && pile->snd != snd);
        }
        return pt_pile_snd != index;
    }
    return false;
}

//72254
s32 vol_r(s16 a1, s16 a2) {
    s32 result = (a1 * snd_sqrt_table[a2]) >> 8;
    return result;
}

//7226C
s32 vol_l(s16 a1, s16 a2) {
    s32 result = (a1 * snd_sqrt_table[127 - a2]) >> 8;
    return result;
}

//7228C
void PlaySnd(s16 snd, s16 obj_id) {
    // 3DS port: once Betilla has flown off the screen after giving her power, her flying animation keeps
    // looping (she stays active) and replays her sparkle sound about every second, fading for some 20
    // seconds. Skip her sounds once she is entirely off the screen.
    if (obj_id >= 0 && obj_id < level.nb_objects && level.objects[obj_id].type == TYPE_153_FEE) {
        obj_t* fee = level.objects + obj_id;
        s32 feet_y = fee->screen_y + fee->offset_by;
        if (feet_y < 0 || fee->screen_y > SCREEN_HEIGHT || fee->screen_x + fee->offset_bx * 2 < 0 || fee->screen_x > SCREEN_WIDTH) {
            return;
        }
    }
    if (CarteSonAutorisee) {
        if ((ray.scale != 0 && obj_id == reduced_rayman_id) || obj_id == rayman_obj_id) {
            obj_id = -1;
        }
        if (obj_id == -1 && snd != 15) {
            indice_ray_wait = 0;
        }
        s16 sound_to_interrupt = last_snd(obj_id);
//        printf("playing sound %d: want to interrupt sound %d for obj %d\n", sound_id, sound_to_interrupt, obj_id);
        if (sound_to_interrupt != snd && (sound_table[sound_to_interrupt] & 8) != 0) {
            erase_pile_snd(obj_id);
            s16 voice_id = get_voice_obj_snd(obj_id, sound_to_interrupt);
            if (voice_id >= 0) {
                voice_t* voice = voice_table + voice_id;
                sound_table_entry_t* sound = hard_sound_table + voice->snd;
                KeyOff(voice_id, bank_to_use[voice->snd], sound->prog, sound->tone, sound->note);
                voice->obj = -2;
                voice->snd = -1;
                voice_is_working[voice_id] = 0;
            }
        }

        u8 pan_snd = 0;
        u8 vol_snd = 0;
        if (!dans_la_map_monde && snd != 0 && (snd != sound_to_interrupt || (sound_table[sound_to_interrupt] & 4) != 0)) {
            if (obj_id == -1 || (obj_id == rayman_obj_id && rayman_obj_id != -1)) {
                pan_snd = get_pan_snd(&ray);
                vol_snd = 127;
            } else {
                pan_snd = get_pan_snd(level.objects + obj_id);
                if (sound_table[snd] & 1) {
                    vol_snd = get_vol_snd(level.objects + obj_id);
                } else {
                    vol_snd = 127;
                }
            }
        }

        s16 prog = hard_sound_table[snd].prog;
        u8 tone = hard_sound_table[snd].tone;
        u8 note = hard_sound_table[snd].note;

        switch(snd) {
            case 53:
                if (level.objects[obj_id].type != TYPE_238_POING_FEE) {
                    prog = hard_sound_table[53].prog;
                    tone = hard_sound_table[53].tone;
                    note = hard_sound_table[53].note;
                } else {
                    // Betilla's fist bringing her text on screen. Fixed (3DS port): as in the PS1 code, the
                    // recording also comes from entry 0, which has none, so this whoosh is silent. Only the
                    // note was taken from entry 0 (note 0), which played sound 53's recording at about 470 Hz:
                    // a 6-second low ring, overlapping for each line of her text.
                    prog = hard_sound_table[0].prog;
                    tone = hard_sound_table[0].tone;
                    note = hard_sound_table[0].note;
                }
                break;
            case 47:
                if (num_world != 5) {
                    prog = hard_sound_table[47].prog;
                    tone = hard_sound_table[47].tone;
                    note = hard_sound_table[47].note;
                } else {
                    prog = hard_sound_table[170].prog;
                    tone = hard_sound_table[170].tone;
                    note = hard_sound_table[170].note;
                }
                break;
            case 15:
                ++indice_ray_wait;
                if (indice_ray_wait > 2) {
                    indice_ray_wait = 0;
                    prog = hard_sound_table[15].prog;
                    tone = hard_sound_table[15].tone;
                    note = hard_sound_table[15].note;
                }
                erase_pile_snd(-1);
                break;
            case 80:
                ++indice_trz_wait;
                if (indice_trz_wait > 1) {
                    prog = hard_sound_table[80].prog;
                    tone = hard_sound_table[80].tone;
                    note = hard_sound_table[80].note;
                    indice_trz_wait = 0;
                }
                break;
            case 14:
                prog = hard_sound_table[14].prog;
                tone = hard_sound_table[14].tone;
                note = hard_sound_table[14].note + not_snd_wiz[level.objects[obj_id].sub_etat - 24]; // ting note depends on the sub_etat
                break;
            case 19:
                prog = hard_sound_table[19].prog;
                tone = hard_sound_table[19].tone;
                note = hard_sound_table[19].note;
                break;
            case 103:
                ps1_guitar_start(obj_id); // 3DS port: the Musician's guitar (PS1 only; see ps1_guitar_start)
                prog = hard_sound_table[103].prog;
                tone = hard_sound_table[103].tone;
                note = hard_sound_table[103].note;
                break;
            case 245:
                prog = hard_sound_table[245].prog;
                tone = hard_sound_table[245].tone;
                note = hard_sound_table[245].note;
                break;
            // It seems these are PS1 only:
            /*case 55:
                prog = -1;
                if ((s16) PS1_SongIsPlaying(0xc) == 0)
                {
                    PS1_PlaySnd(0xc, 0);
                }
                break;
            case 57:
                prog = -1;
                PS1_StopPlayingSnd(0xc);
                break;
            case 103:
                prog = -1;
                if (SsIsEos(PS1_SepInfos[22].access_num, PS1_SepInfos[22].seq_num) == 0)
                {
                    PS1_PlaySnd(0x16, 0);
                    D_801CEFCC = true;
                    D_801CEFCE = obj_id;
                }
                break;*/
            default:
                prog = hard_sound_table[snd].prog;
                tone = hard_sound_table[snd].tone;
                note = hard_sound_table[snd].note;
                break;
        }

        // Fix (3DS port): sounds whose table entry has no recording (prog 255, e.g. sound 3 = Rayman
        // releasing the fist) used to skip this whole block, including the bookkeeping below that
        // records the object's last sound. The PS1 code (rayman-ps1-decomp) only treats prog == -1
        // as "nothing to do" and still records the sound. Without this, Rayman's last sound stayed
        // "fist wind-up" after a punch, so the next wind-up counted as already playing and started at
        // volume 0 - silent on the Moskito ride, where Rayman makes no other sounds between punches.
        bool has_sample = (prog != 255);
        if (prog != -1) {
            erase_pile_snd(obj_id);
            s32 voll = vol_l(Volume_Snd * vol_snd * hard_sound_table[snd].volume >> 14, pan_snd); // NOTE: has no effect?
            s32 volr = vol_r(Volume_Snd * vol_snd * hard_sound_table[snd].volume >> 14, pan_snd); // NOTE: has no effect?

            if (frame_snd[snd] == 0) {
                // NOTE: the PS1 version implements left/right directional sound here, using SsUtKeyOn
                keyon_loop_allowed = (sound_table[snd] & 0x10) != 0;
                s32 voice_id = has_sample ? KeyOn(bank_to_use[snd], prog, tone, note, Volume_Snd * vol_snd * hard_sound_table[snd].volume >> 14, pan_snd) : -1;
                if (voice_id != -1) {
                    erase_voice_table(obj_id);
                    voice_table[voice_id].obj = obj_id;
                    voice_table[voice_id].vol = vol_snd;
                    voice_table[voice_id].pan = pan_snd;
                    voice_table[voice_id].snd = snd;
                    if ((sound_table[snd] & 0x10) != 0) {
                        voice_is_working[voice_id] = true;
                    }
                }
            } else {
                // NOTE: I don't yet know what causes the sound to be emitted in this code path.
                // So, I added this for now (delete again when it's figured out).
                keyon_loop_allowed = (sound_table[snd] & 0x10) != 0;
                s32 delayed_voice_id = has_sample ? KeyOn(bank_to_use[snd], prog, tone, note, Volume_Snd * vol_snd * hard_sound_table[snd].volume >> 14, pan_snd) : -1;
                // Fix (3DS port): record this voice like the path above (the PS1 does so when it plays a
                // delayed sound), so the sound can be interrupted and manage_snd() can stop it. Untracked,
                // a looping sound played this way (e.g. sound 125, a flying bug's buzz) could never be
                // stopped, and each new buzz added another copy that got louder and louder.
                if (delayed_voice_id != -1) {
                    erase_voice_table(obj_id);
                    voice_table[delayed_voice_id].obj = obj_id;
                    voice_table[delayed_voice_id].vol = vol_snd;
                    voice_table[delayed_voice_id].pan = pan_snd;
                    voice_table[delayed_voice_id].snd = snd;
                    if ((sound_table[snd] & 0x10) != 0) {
                        voice_is_working[delayed_voice_id] = true;
                    }
                }
//                printf("Debug: snd %d, prog %d, tone %d, note %d, vol %d, pan %d\n", snd, prog, tone, note, Volume_Snd * vol_snd * hard_sound_table[snd].volume >> 14, pan_snd);

                nettoie_pile_snd();
                erase_pile_snd(obj_id);
                pile_snd[pt_pile_snd].obj = obj_id;
                pile_snd[pt_pile_snd].snd = snd;
                pile_snd[pt_pile_snd].prog = prog;
                pile_snd[pt_pile_snd].tone = tone;
                pile_snd[pt_pile_snd].note = note;
                pile_snd[pt_pile_snd].vol = hard_sound_table[snd].volume;
                pile_snd[pt_pile_snd].field_C = vol_snd;
                pile_snd[pt_pile_snd].pan = pan_snd;
                pile_snd[pt_pile_snd].end_time = frame_snd[snd] + map_time;
                if (snd_bis[snd] != 0) {
                    pile_snd[pt_pile_snd].field_14 = 1;
                } else {
                    pile_snd[pt_pile_snd].field_14 = 0;
                }
                if (pt_pile_snd < 9) {
                    ++pt_pile_snd;
                }
            }
            if ((snd == 203 || snd == 237 || snd == 209 || snd == 225 || snd == 236) && (dead_time == 64 || dead_time == 128)) {
                if (ps1_music_enabled) {
                    ps1_music_boss_beaten(); // PS1 (start_cd_bbdead): fade out, victory jingle
                } else {
                    start_cd_bbdead();
                }
            }
            for (s32 i = 0; i < 20; ++i) {
                if (stk_obj[i] == obj_id || stk_obj[i] == -2) {
                    stk_obj[i] = obj_id;
                    stk_snd[i] = snd;
                    return;
                }
            }
            if (snd != -1) {
                s32 index = 19;
                while(level.objects[stk_obj[index]].is_active) {
                    --index;
                    if (index == -1) {
                        index = 0;
                        break;
                    }
                }
                stk_obj[index] = obj_id;
                stk_snd[index] = snd;
            }
        }

    }
}

//72960
void PlaySnd_old(s16 sound_id) {
    s16 voice_index = -1;
    if (CarteSonAutorisee) {
        u8 prog = hard_sound_table[sound_id].prog;
        if (prog != 255) {
            keyon_loop_allowed = (sound_table[sound_id] & 0x10) != 0;
            voice_index = KeyOn(bank_to_use[sound_id], prog,
                                hard_sound_table[sound_id].tone,
                                hard_sound_table[sound_id].note,
                                Volume_Snd * hard_sound_table[sound_id].volume >> 7, 64);
            if (voice_index >= 0 && voice_index < (s32)COUNT(digi_voices)) {
                // 3DS port: a sound played this way (not tied to an object) restarts rather than playing
                // over its still-sounding previous copy. E.g. the thunder in the stormy levels (sound
                // 195, 2.25 s) comes at random intervals, sometimes only half a second apart, and two
                // copies slightly out of step sounded like an echo.
                for (s32 i = 0; i < (s32)COUNT(digi_voices); ++i) {
                    if (i != voice_index && digi_voices[i].is_playing && digi_voices[i].data == digi_voices[voice_index].data) {
                        digi_voices[i].is_playing = false;
                    }
                }
            }
            if (voice_index != -1) {
                voice_table[voice_index].obj = -2;
                voice_table[voice_index].vol = 64;
                voice_table[voice_index].pan = 64;
                if (sound_table[sound_id] & 0x10) {
                    voice_is_working[voice_index] = 1;
                }
            }
        }
    }
}

//72A1C
void setvol(s16 obj_id) {
    print_once("Not implemented: setvol"); //stub
}

//72E74
void setpan(s16 obj_id) {
    print_once("Not implemented: setpan"); //stub
}

//72F38
// Implemented for the 3DS port, following the PS1 code (FUN_80168f48 and the second half of setvol in
// rayman-ps1-decomp), which runs every frame:
//  - a continuous (looping) sound whose object has gone (deactivated, or no longer tracked) is stopped;
//  - the volume of each continuous sound follows the distance between its object and Rayman.
// Without this, a looping sound (e.g. a flying bug's buzz) played forever once started, piling up
// louder and louder and eventually taking every sound channel, which also left the game silent
// after pausing (the pause mutes all channels, and nothing turned the looping ones back up).
void manage_snd(void) {
    ps1_guitar_update(); // (3DS port)
    for (s32 i = 0; i < COUNT(voice_table); ++i) {
        voice_t* voice = voice_table + i;
        if (voice->snd < 0 || voice->snd > 255 || !(sound_table[voice->snd] & 0x10)) {
            continue;
        }
        bool orphaned = (voice->obj == -2) ||
                        (voice->obj >= 0 && (voice->obj >= level.nb_objects || !level.objects[voice->obj].is_active));
        if (!digi_voices[i].is_playing) {
            // Finished by itself (not a looping sample): just forget it.
            voice_is_working[i] = 0;
            voice->obj = -2;
            voice->snd = -1;
            continue;
        }
        if (orphaned) {
            KeyOff(i, 0, 0, 0, 0);
            voice_is_working[i] = 0;
            for (s32 k = 0; k < COUNT(stk_obj); ++k) {
                if (stk_obj[k] == voice->obj) {
                    stk_snd[k] = -1;
                    break;
                }
            }
            voice->obj = -2;
            voice->snd = -1;
            continue;
        }
        if (voice_is_working[i]) {
            s32 vol = 127;
            if (voice->obj >= 0 && voice->obj != rayman_obj_id && voice->snd != 0xCC) {
                vol = get_vol_snd(level.objects + voice->obj);
            }
            voice->vol = (s16)vol;
            KeyVol(i, Volume_Snd * vol * hard_sound_table[voice->snd].volume >> 14, voice->pan);
        }
    }
}

//730DC
void mute_snd_bouclant(void) {
    // Silence the continuous sounds (PS1 version); manage_snd() turns them back up next frame.
    for (s32 i = 0; i < COUNT(voice_table); ++i) {
        s16 snd = voice_table[i].snd;
        if (snd == 6 || snd == 245 || snd == 2 || (snd >= 0 && snd <= 255 && (sound_table[snd] & 0x10))) {
            KeyVol(i, 0, 0);
        }
    }
}

//73138
void mute_snd(void) {
    for (s32 i = 0; i < COUNT(voice_table); ++i) {
        KeyVol(i, 0, 0);
    }
    print_once("Not implemented: mute_snd"); //stub
}

//73164
void start_freeze_snd(void) {
    mute_snd();
    was_in_freeze = 1;
}

//73178
void stop_freeze_snd(void) {
    was_in_freeze = 0;
}

//7319C
void PlayTchatchVignette(s32 snd) {
    if (CarteSonAutorisee) {
        if (snd == 0) {
            switch(num_world_choice) {
                case 1: {
                    snd = 1;
                } break;
                case 3: {
                    snd = 2;
                } break;
                case 4: {
                    snd = 4;
                } break;
                case 5: {
                    if (num_level_choice != 3) {
                        return;
                    }
                    snd = 3;
                } break;
                case 6: {
                    snd = 7;
                } break;
                default: break;
            }
        }
        PlayTchatch(snd, Volume_Snd); //TODO
    }
}

//73204
void TestCdLoop(void) {
    if (ps1_music_enabled) {
        // PS1 soundtrack: start this level's music once; ps1_music.c handles sequencing and looping.
        if (cdTime == 0) {
            ps1_play_music(num_world, num_level);
            cdTime = 1;
        }
        ps1_music_update_in_level(dead_time != 64);
        return;
    }
    if (cdTime != 0) {
        if (cdTime != -1) {
            //dos_get_time_of_day();
            cdTime = 10 * (cdTimeEnd = cdTimeStart) / 182;
            s32 duration = timeCd[22 * (num_world - 1) + num_level];
            cdTime += 2;
            if (cdTime >= duration) {
                if (flagCDPlay != 0) {
                    if (flagCDPlay >= 4) {
                        //dos_get_time_of_day();
                        flagCDPlay = 0;
                        cdTime = 0;
                        phaseCd = !phaseCd;
                        //sub_2A654(map_cd_tracks[22 * (num_world - 1) + num_level -1 ]); // cd loop?
                    } else {
                        ++flagCDPlay;
                    }
                } else {
                    ++flagCDPlay;
                    //sub_2A6C4();
                }
            }
        }
    } else {
        play_cd_track(map_cd_tracks[22 * (num_world - 1) + num_level -1 ], true);
        phaseCd = 0;
        cdTime = 1;
    }
}

//73384
void InitMusic(void) {
    MusicCdActive = 1;
    ps1_music_init();
    print_once("Not implemented: InitMusic"); //stub
}

//733BC
void DoneMusic(void) {
    print_once("Not implemented: DoneMusic"); //stub
}
