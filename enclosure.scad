// Built for OpenSCAD version 2021.01

panel_x = 256;
panel_y = 128;
panel_z = 14;
screw_inset = 8;
border = 20;

show_electronics = false;
explode = 20;

$fn=16;

module panel() {
    inserts = [
        for (x = [screw_inset, panel_x / 2, panel_x - screw_inset])
        for (y = [screw_inset, panel_y - screw_inset])
        [x, y]
    ];
    insert_inner = 1.5;
    insert_outer = 2.5;

    color("#555")
    difference() {
        cube([panel_x, panel_y, panel_z]);

        for (p = inserts)
        translate([p[0], p[1], 4])
        cylinder(12, insert_outer, insert_outer);
    };

    color("#FB0")
    for (p = inserts)
    translate([p[0], p[1], 4])
    difference() {
        cylinder(10, insert_outer, insert_outer);

        translate([0, 0, 2])
        cylinder(12, insert_inner, insert_inner);
    }
}

if (show_electronics)
translate([border,border,0]) {
    panel();
    translate([0, panel_y, 0]) panel();
}

if (show_electronics)
color("#0A0")
translate([border - 13, border + 1.5*panel_y - 45/2, panel_z])
cube([65, 45, 10]);


for (a = [0, 90, 180, 270])
translate([border + panel_x/2, border + panel_y])
rotate(a)
translate([-border - panel_x/2 - explode, -border-panel_y - explode])
if (a == 270) difference() {
    border();

    translate([border + panel_y/2 - 46/2, border - 15, panel_z + 1 - 0.1])
    cube([46, 65, 10.1]);

    translate([border + panel_y/2 - 46/2, border - 15, panel_z + 1 - 0.1])
    rotate([-45,0,0])
    cube([46, 65, 12]);

    translate([border + panel_y/2 - 46/2 + 7.5, 15, panel_z + 5])
    rotate([90, 0, 0])
    cylinder(30, 1.5);
} else border(a == 0 || a == 180);

module border(left) {
    arm = panel_y;
    difference() {
        union() {
            cube([border, border + arm, panel_z + 0.01]);

            translate([0, 0, panel_z])
            cube([border /*+ 16*/, border + panel_x - arm, panel_z]);

            cube([border + panel_x - arm, border, panel_z + 0.01]);

            translate([0, 0, panel_z])
            cube([border + panel_x - arm, border /*+ 16*/, panel_z]);

            translate([border, border, panel_z])
            linear_extrude(panel_z)
            polygon([
                [0, 0],
                [4.24*screw_inset, 0],
                [0, 4.24*screw_inset],
            ]);

            translate([border, border + panel_y, panel_z])
            linear_extrude(panel_z)
            polygon([
                [0, 4*screw_inset],
                [2*screw_inset, 2*screw_inset],
                [2*screw_inset, -2*screw_inset],
                [0, -4*screw_inset],
            ]);

            translate([10, border + arm, 9])
            rotate([0, 90, 0])
            tab();

            translate([12, border + arm, panel_z+7])
            rotate([0, 90, 0])
            tab(16);

            translate([border, border + arm + 15, panel_z + 7])
            rotate([0, 90, 90])
            tab(30);

            if (!left)
            translate([border + 2*screw_inset, border + arm, panel_z + 7])
            rotate([0, 90, -90])
            tab(2*screw_inset);
        }

        translate([border + arm, 10, 9])
        rotate([270, 90, 0])
        tab();

        translate([border + arm, 12, panel_z + 7])
        rotate([270, 90, 0])
        tab(16);

        translate([border + arm - 14.995, border, panel_z + 7])
        rotate([180, 90, 0])
        tab(30.01);

        for (p = [
            [border + screw_inset, border + screw_inset],
            each left ? [
                [border + panel_x/2, border + screw_inset],
                [border + screw_inset, border + panel_x/2 - screw_inset],
                [border + screw_inset, border + panel_x/2 + screw_inset],
            ] : [
                [border + screw_inset, border + panel_x/2],
                //[border + panel_x/2 - screw_inset, border + screw_inset],
            ],
        ])
        translate([p[0], p[1], panel_z])
        screw_hole();
    }
}

// Center vertical
translate([border + panel_x/2, border + panel_y, panel_z + explode])
difference() {
    w = screw_inset;
    h = panel_y-2*w-0.25;

    post_diam = 7.5;
    hanger_diam = 7;
    hanger_len = 7;

    union() {
        linear_extrude(panel_z)
        polygon([
            [w*2, h],
            [w*2, -h],
            [-w*2, -h],
            [-w*2, h]
        ]);

    }

    for (a = [0,180])
    rotate([0,0,a])
    translate([2*w, 70, -0.5])
    rotate([0,0,180])
    linear_extrude(panel_z+1)
    polygon([
        [-w, 3*w],
        [w, w],
        [w, -w],
        [-w, -3*w],
    ]);

    cw = 2;
    ch = 4;

    for (y = [-95, 95, -45, 45])
    translate([0,y,0])
    rotate([90,0,90])
    linear_extrude(5*w, center=true)
    polygon([
        [-cw - ch - 1, -1],
        [-cw, ch],
        [cw, ch],
        [cw + ch + 1, -1]
    ]);

    translate([0, -h, panel_z/2])
    rotate([0,90,0])
    tab(2*w+0.5);

    translate([0, h, panel_z/2])
    rotate([0,90,180])
    tab(2*w+0.5);

    translate([0, screw_inset])
    screw_hole();

    translate([0, -screw_inset])
    screw_hole();

    for (y = [-25, 25])
    translate([0, y, -1]) {
        cylinder(panel_z+2, d=6);
        cylinder(1+1, d=18);
    }
}

module screw_hole() {
    translate([0, 0, 3.49])
    cylinder(20, d = 8);

    translate([0,0, 1])
    cylinder(2.5, d1=3, d2=8);

    translate([0, 0, -1])
    cylinder(20, d = 3);
}

module tab(h=12) {
    linear_extrude(h, center=true)
    polygon([
        [5, -0.01],
        [0, 5],
        [-5, -0.01],
    ]);
}
