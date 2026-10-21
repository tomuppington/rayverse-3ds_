// Crawl assist (added for the 3DS port), shared by the 3DS and headless test platforms.
//
// On the console versions you crawl by holding Down and pressing Left/Right. The PC version only
// crawls while the Jump button is held too, and the order matters: Jump pressed while *walking*
// always makes Rayman jump, even with Down held; only from a standstill does Down + Jump crouch.
//
// So while Down and a direction are both held, this adjusts what the game sees:
//   - walking/standing, not yet crouched: hide the direction, so Rayman stops and crouches (Down);
//   - crouched, or already crawling/turning while crouched: add Jump, so he crawls (and turns
//     around if needed) in the direction pressed;
//   - anything else (jumping, hanging, ...): leave the input untouched.

typedef struct crawl_assist_t {
    bool hide_direction;
    bool add_jump;
} crawl_assist_t;

static crawl_assist_t crawl_assist(bool down, bool left, bool right, bool up) {
    crawl_assist_t r = {false, false};
    if (!down || up || !(left || right)) {
        return r;
    }
    u8 m = ray.main_etat;
    u8 s = ray.sub_etat;
    bool crawling = (m == 1 && (s == 8 || s == 9 || s == 10 || s == 11)) ||
                    (m == 0 && (s == 50 || s == 51)); // turning around while crouched
    bool crouched = (m == 0 && (s == 15 || s == 47 || s == 48));
    if (crawling || crouched) {
        r.add_jump = true;
    } else if (m == 0 || m == 1 || m == 3) {
        r.hide_direction = true; // on the ground but not crouched yet: stop and crouch first
    }
    return r;
}
