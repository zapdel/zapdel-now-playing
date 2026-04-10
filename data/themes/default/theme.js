// theme.js — default theme
// np-core.js handles all DOM updates and visibility logic.
// The default theme relies entirely on np-core.js defaults:
//   - CSS classes (.np-showing / .np-hiding / .np-hidden) drive slide animations
//   - Visibility mode "on_track_change" shows for 8 seconds then hides
//
// Override any of the hooks below to customize this theme's behavior.
// See np-core.js for the full hook protocol.

// window.__npTheme = {
//
//   // How long to show the widget after a track change (on_track_change mode).
//   // Default: 8000 ms
//   displayDuration: 8000,
//
//   // How long the hide animation takes; must match --np-anim-out-duration in CSS.
//   // Default: 350 ms
//   animOutDuration: 350,
//
//   // Called after np-core updates the DOM each tick.
//   onUpdate: function (payload) { },
//
//   // Replace the built-in CSS-class show/hide with custom animation logic.
//   onShow: function () { },
//   onHide: function () { },
// };
