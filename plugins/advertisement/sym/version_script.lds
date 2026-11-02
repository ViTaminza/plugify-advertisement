{
    global:
        Plugify_*;

        /* s2sdk binding slots (defined by s2sdk_bindings, filled by the plugify module from the
           s2sdk dependency). No other plugin API is consumed: advertisement is pure s2sdk and has
           no hudmenu/pisexcompat bindings. */
        __s2sdk_*;

        /* No exported methods: advertisement exposes nothing to other plugins. */

    local: *;
};
