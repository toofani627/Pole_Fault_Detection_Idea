// ============================================================================
//  ELECTRONIC PROTOTYPE ENCLOSURE — Parameterised OpenSCAD Model
//  All tuneable dimensions are declared here as global variables.
//  Modify values in Sections 1–8; derived math lives in Section 9.
// ============================================================================


// ============================================================================
//  1. ENCLOSURE CORE DIMENSIONS
// ============================================================================
inner_length            = 80;       // X – internal cavity length
inner_width             = 80;       // Y – internal cavity width
inner_height            = 140;      // Z – internal cavity height
wall_thickness          = 5;        // Shell wall thickness (uniform)
internal_fillet_radius  = 5;        // Corner radius inside the cavity
roof_angle              = 20;       // Slope angle of the angled roof (degrees)


// ============================================================================
//  2. MOUNTING TAB PARAMETERS
//     Tabs sit on the rear inner wall and hold the gate via screws.
// ============================================================================
tab_length              = 7;        // Tab extent along X
tab_breadth             = 2.5;      // Tab extent along Y (depth into cavity)
tab_height              = 7;        // Tab extent along Z
tab_hole_dia            = 3;        // Screw-hole diameter on each tab

// --- Tab placement (relative to inner cavity) ---
tab_dist_from_back      = 0;        // Gap between tab rear face and back wall
tab_dist_from_side      = 5;        // Gap between tab side face and side wall
tab_z_low               = 30;       // Z of the lower tab pair
tab_z_high              = 100;      // Z of the upper tab pair


// ============================================================================
//  3. FRONT PANEL FEATURES
//     A large central hole + a pentagon pattern of smaller holes.
// ============================================================================
front_main_hole_dia     = 30;       // Central connector / display hole diameter
front_pent_radius       = 25;       // Pentagon pattern radius (centre-to-hole)
front_pent_hole_dia     = 5;        // Diameter of each pentagon hole
front_pent_hole_count   = 5;        // Number of holes in the pattern
front_pent_start_angle  = 90;       // Rotation offset so first hole is at top


// ============================================================================
//  4. FLOOR SLOT PARAMETERS
//     A rectangular slot cut through the bottom wall.
// ============================================================================
floor_slot_length       = 10;       // Slot extent along X
floor_slot_width        = 2.3;      // Slot extent along Y
floor_slot_y_offset     = -40;      // Y offset from the centred position


// ============================================================================
//  5. LEFT WALL HOLE
//     A single round hole on the left (X = 0) wall.
// ============================================================================
left_hole_y_pos         = 45;       // Y position of hole centre
left_hole_z_pos         = 50;       // Z position of hole centre
left_hole_dia           = 5;        // Hole diameter


// ============================================================================
//  6. ROOF HOLE PARAMETERS
//     Two holes punched vertically through the angled roof.
// ============================================================================
roof_hole_dia           = 5;        // Diameter of each roof hole
roof_hole_x_inset       = 15;       // Distance from each X-edge to hole centre


// ============================================================================
//  7. REAR GATE (DOOR) PARAMETERS
// ============================================================================
gate_thickness          = 3;        // Gate plate thickness
gate_side_margin        = 10;       // Total X margin (gate is narrower by this)
gate_preview_gap        = 10;       // Gap when showing gate beside cabin


// ============================================================================
//  8. RENDER & APPEARANCE SETTINGS
// ============================================================================
render_mode             = "both";   // "cabin", "gate", or "both"
color_body              = "LightSeaGreen";
color_roof              = "DarkSlateGray";
color_gate              = "DarkCyan";
roof_thickness          = 3;        // Slab thickness of the angled roof cap

// Curve resolution
fn_fillet               = 50;       // $fn for fillet cylinders
fn_hole_large           = 60;       // $fn for large holes (≥ 10 mm)
fn_hole_small           = 30;       // $fn for small holes (< 10 mm)

// Boolean-cut tolerance
eps                     = 0.1;      // Tiny overlap to prevent z-fighting
bool_overshoot          = 2;        // Extra depth for clean boolean cuts


// ============================================================================
//  9. DERIVED / CALCULATED DIMENSIONS  (do not edit directly)
// ============================================================================
outer_length            = inner_length  + (2 * wall_thickness);
outer_width             = inner_width   + (2 * wall_thickness);
outer_height            = inner_height  + wall_thickness;
external_fillet_radius  = internal_fillet_radius + wall_thickness;

// Roof geometry
drop_amount             = outer_width * tan(roof_angle);
front_cut_height        = outer_height - drop_amount;

// Gate derived size
gate_width              = inner_length - gate_side_margin;

// Congruent gate-hole offsets (so gate holes line up with tab holes)
gate_hole_x_rel         = (wall_thickness + tab_dist_from_side + tab_length / 2)
                          - ((outer_length - gate_width) / 2);
gate_hole_z_low_rel     = (tab_z_low  + tab_height / 2) - wall_thickness;
gate_hole_z_high_rel    = (tab_z_high + tab_height / 2) - wall_thickness;

// Front-panel centre
front_center_x          = outer_length / 2;
front_center_z          = front_cut_height / 2;

// Floor-slot position
floor_slot_x            = (outer_length - floor_slot_length) / 2;
floor_slot_y            = ((outer_width - floor_slot_width) / 2) + floor_slot_y_offset;

// Roof-hole X positions
roof_hole_x_left        = roof_hole_x_inset;
roof_hole_x_right       = outer_length - roof_hole_x_inset;
roof_hole_y             = outer_width / 2;


// ============================================================================
//  10. BASE GEOMETRY MODULES
// ============================================================================

// A box with rounded vertical edges (2-D hull extruded along Z).
module rounded_block(l, w, h, r) {
    hull() {
        translate([r,   r,   0]) cylinder(h = h, r = r, $fn = fn_fillet);
        translate([l-r, r,   0]) cylinder(h = h, r = r, $fn = fn_fillet);
        translate([r,   w-r, 0]) cylinder(h = h, r = r, $fn = fn_fillet);
        translate([l-r, w-r, 0]) cylinder(h = h, r = r, $fn = fn_fillet);
    }
}

// Outer shell minus inner cavity.
module main_body() {
    difference() {
        rounded_block(outer_length, outer_width, outer_height, external_fillet_radius);
        translate([wall_thickness, wall_thickness, wall_thickness])
            rounded_block(inner_length, inner_width, inner_height + 50, internal_fillet_radius);
    }
}


// ============================================================================
//  11. FEATURE MODULES — Holes, Slots & Tabs
// ============================================================================

// --- Front panel: central hole + pentagon pattern ---
module front_interface_holes() {
    // Main connector / display hole
    translate([front_center_x, -1, front_center_z])
        rotate([-90, 0, 0])
            cylinder(h = wall_thickness + bool_overshoot, d = front_main_hole_dia, $fn = fn_hole_large);

    // Pentagon pattern of smaller holes
    for (i = [0 : front_pent_hole_count - 1]) {
        angle = front_pent_start_angle + (i * (360 / front_pent_hole_count));
        x_off = front_pent_radius * cos(angle);
        z_off = front_pent_radius * sin(angle);
        translate([front_center_x + x_off, -1, front_center_z + z_off])
            rotate([-90, 0, 0])
                cylinder(h = wall_thickness + bool_overshoot, d = front_pent_hole_dia, $fn = fn_hole_small);
    }
}

// --- Floor slot ---
module floor_offset_slot() {
    translate([floor_slot_x, floor_slot_y, -1])
        cube([floor_slot_length, floor_slot_width, wall_thickness + bool_overshoot]);
}

// --- Left-wall hole ---
module left_wall_hole() {
    translate([-1, left_hole_y_pos, left_hole_z_pos])
        rotate([0, 90, 0])
            cylinder(h = wall_thickness + bool_overshoot, d = left_hole_dia, $fn = fn_hole_small);
}

// --- Roof holes (vertical, through the angled cap) ---
module roof_top_holes() {
    for (x_pos = [roof_hole_x_left, roof_hole_x_right])
        translate([x_pos, roof_hole_y, 0])
            cylinder(h = outer_height * 2, d = roof_hole_dia, center = true, $fn = fn_hole_small);
}

// --- Mounting tabs (left + right pairs at two Z heights) ---
module mounting_tabs() {
    for (z_pos = [tab_z_low, tab_z_high]) {
        // Left tab
        translate([
            wall_thickness + tab_dist_from_side,
            outer_width - wall_thickness - tab_breadth - tab_dist_from_back,
            z_pos
        ])
        difference() {
            cube([tab_length, tab_breadth, tab_height]);
            translate([tab_length / 2, -1, tab_height / 2])
                rotate([-90, 0, 0])
                    cylinder(h = tab_breadth + bool_overshoot, d = tab_hole_dia, $fn = fn_hole_small);
        }

        // Right tab (mirrored in X)
        translate([
            outer_length - wall_thickness - tab_length - tab_dist_from_side,
            outer_width - wall_thickness - tab_breadth - tab_dist_from_back,
            z_pos
        ])
        difference() {
            cube([tab_length, tab_breadth, tab_height]);
            translate([tab_length / 2, -1, tab_height / 2])
                rotate([-90, 0, 0])
                    cylinder(h = tab_breadth + bool_overshoot, d = tab_hole_dia, $fn = fn_hole_small);
        }
    }
}


// ============================================================================
//  12. REAR GATE MODULE (Standalone Part)
// ============================================================================
module rear_gate() {
    color(color_gate)
    difference() {
        // Rounded plate matching internal fillet
        translate([internal_fillet_radius, internal_fillet_radius, 0])
        hull() {
            r = internal_fillet_radius;
            w = gate_width - 2 * r;
            h = inner_height - 2 * r;
            cylinder(h = gate_thickness, r = r, $fn = fn_fillet);
            translate([w, 0, 0]) cylinder(h = gate_thickness, r = r, $fn = fn_fillet);
            translate([0, h, 0]) cylinder(h = gate_thickness, r = r, $fn = fn_fillet);
            translate([w, h, 0]) cylinder(h = gate_thickness, r = r, $fn = fn_fillet);
        }

        // Screw holes — positions match the mounting tabs
        for (z_off = [gate_hole_z_low_rel, gate_hole_z_high_rel]) {
            translate([gate_hole_x_rel, z_off, -1])
                cylinder(h = gate_thickness + bool_overshoot, d = tab_hole_dia, $fn = fn_hole_small);
            translate([gate_width - gate_hole_x_rel, z_off, -1])
                cylinder(h = gate_thickness + bool_overshoot, d = tab_hole_dia, $fn = fn_hole_small);
        }
    }
}


// ============================================================================
//  13. FINAL ASSEMBLY
// ============================================================================

// --- Cabin (main enclosure body + roof) ---
if (render_mode == "cabin" || render_mode == "both") {
    union() {
        // Shell with all boolean cuts
        color(color_body)
        difference() {
            union() {
                main_body();
                mounting_tabs();
            }

            // Angled roof cut
            translate([-10, -5, front_cut_height])
                rotate([roof_angle, 0, 0])
                    cube([outer_length + 20, outer_width * 3, 300]);

            // Rear gate opening
            translate([(outer_length - gate_width) / 2,
                       inner_width + wall_thickness - 1,
                       wall_thickness - eps])
                cube([gate_width, wall_thickness + bool_overshoot, inner_height + 20]);

            // Feature holes & slots
            front_interface_holes();
            floor_offset_slot();
            left_wall_hole();
        }

        // Angled roof cap (separate colour to prevent z-fighting)
        difference() {
            color(color_roof)
            intersection() {
                translate([0, 0, front_cut_height])
                    rotate([roof_angle, 0, 0])
                        translate([-10, -20, 0])
                            cube([outer_length + 20, outer_width * 3, roof_thickness]);

                translate([0.025, 0.025, -eps])
                    rounded_block(outer_length - 0.05, outer_width - 0.05,
                                  outer_height + 100, external_fillet_radius);
            }
            roof_top_holes();
        }
    }
}

// --- Rear gate ---
if (render_mode == "gate") {
    rear_gate();
} else if (render_mode == "both") {
    // Preview: gate placed behind the cabin with a small gap
    translate([(outer_length - gate_width) / 2, outer_width + gate_preview_gap, wall_thickness])
        rotate([90, 0, 0])
            rear_gate();
}
