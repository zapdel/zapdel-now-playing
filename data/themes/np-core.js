/* ==========================================================================
   np-core.js — Now Playing Plugin Core
   Shared by all themes. Handles DOM updates, marquee, and visibility logic.

   Standard element IDs (all optional — core skips any that are absent):
     #np-container       — outer widget wrapper; receives visibility classes
     #np-art-wrap        — album art container (shown/hidden)
     #np-art             — <img> for album artwork
     #np-art-placeholder — fallback shown when no art is available
     #np-pause-overlay   — overlay shown when playback is paused
     #np-title-wrap      — marquee clipping container
     #np-title           — track title text
     #np-artist          — artist name text
     #np-sep             — separator between artist and album (hidden when either is empty)
     #np-album           — album name text
     #np-progress-wrap   — progress bar track (shown/hidden)
     #np-progress-bar    — progress bar fill

   Theme hook protocol (window.__npTheme, all fields optional):
     displayDuration  {number}    ms to stay visible in on_track_change mode (default 8000)
     animOutDuration  {number}    ms for hide animation; delay before applying np-hidden (default 350)
     onShow           {function}  replaces default CSS-class show animation
     onHide           {function}  replaces default CSS-class hide animation
     onUpdate         {function(payload)}  called after core updates the DOM

   Core checks window.__npTheme at call time, so theme.js may load in any order.
   ========================================================================== */

(function () {
    'use strict';

    // -----------------------------------------------------------------------
    // Element cache — all nullable; core skips missing elements silently
    // -----------------------------------------------------------------------

    var container       = document.getElementById('np-container');
    var artWrap         = document.getElementById('np-art-wrap');
    var artImg          = document.getElementById('np-art');
    var artPlaceholder  = document.getElementById('np-art-placeholder');
    var pauseOverlay    = document.getElementById('np-pause-overlay');
    var titleWrap       = document.getElementById('np-title-wrap');
    var titleEl         = document.getElementById('np-title');
    var artistEl        = document.getElementById('np-artist');
    var sepEl           = document.getElementById('np-sep');
    var albumEl         = document.getElementById('np-album');
    var progressWrap    = document.getElementById('np-progress-wrap');
    var progressBar     = document.getElementById('np-progress-bar');

    var hideTimer    = null;
    var currentTitle = '';

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    function themeVal(key, defaultVal) {
        var t = window.__npTheme;
        return (t && t[key] !== undefined) ? t[key] : defaultVal;
    }

    // -----------------------------------------------------------------------
    // Visibility
    // -----------------------------------------------------------------------

    function showWidget() {
        if (hideTimer) {
            clearTimeout(hideTimer);
            hideTimer = null;
        }
        var onShow = themeVal('onShow', null);
        if (typeof onShow === 'function') {
            onShow();
        } else if (container) {
            container.classList.remove('np-hidden', 'np-hiding');
            container.classList.add('np-showing');
        }
    }

    function hideWidget() {
        var onHide = themeVal('onHide', null);
        if (typeof onHide === 'function') {
            onHide();
        } else if (container) {
            container.classList.remove('np-showing');
            container.classList.add('np-hiding');
            var outDuration = themeVal('animOutDuration', 350);
            hideTimer = setTimeout(function () {
                hideTimer = null;
                if (container) {
                    container.classList.remove('np-hiding');
                    container.classList.add('np-hidden');
                }
            }, outDuration);
        }
    }

    // -----------------------------------------------------------------------
    // Marquee
    // -----------------------------------------------------------------------

    function applyMarquee() {
        if (!titleEl || !titleWrap) return;

        titleEl.classList.remove('np-marquee');
        titleEl.style.removeProperty('--np-marquee-offset');

        requestAnimationFrame(function () {
            var wrapWidth = titleWrap.clientWidth;
            var textWidth = titleEl.scrollWidth;

            if (textWidth > wrapWidth) {
                var gapPx = parseInt(
                    getComputedStyle(document.documentElement)
                        .getPropertyValue('--np-marquee-gap')
                ) || 60;
                var offset = -(textWidth + gapPx);
                titleEl.style.setProperty('--np-marquee-offset', offset + 'px');

                var spaces = '\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0';
                titleEl.textContent = currentTitle + spaces + currentTitle;
                titleEl.classList.add('np-marquee');
            }
        });
    }

    // -----------------------------------------------------------------------
    // Main update — called by the plugin via obsNowPlayingUpdate event
    // -----------------------------------------------------------------------

    function update(payload) {
        if (!payload) return;

        // CSS canvas size variables
        if (payload.width)
            document.documentElement.style.setProperty('--np-width',  payload.width  + 'px');
        if (payload.height)
            document.documentElement.style.setProperty('--np-height', payload.height + 'px');

        var title         = payload.title   || '';
        var artist        = payload.artist  || '';
        var album         = payload.album   || '';
        var isPlaying     = !!payload.isPlaying;
        var showAlbumArt  = payload.showAlbumArt !== false;
        var showProgress  = payload.showProgress !== false;
        var visibilityMode = payload.visibilityMode || 'always';
        var hasNewTrack   = !!payload.hasNewTrack;
        var thumbnail     = payload.thumbnail || '';
        var progress      = typeof payload.progress === 'number' ? payload.progress : 0;

        // -- Title --
        if (title !== currentTitle) {
            currentTitle = title;
            if (titleEl) titleEl.textContent = title;
            applyMarquee();
        }

        // -- Artist / Album --
        if (artistEl) artistEl.textContent = artist;
        if (albumEl)  albumEl.textContent  = album;
        if (sepEl)    sepEl.style.display  = (artist && album) ? '' : 'none';

        // -- Album art --
        if (showAlbumArt) {
            if (artWrap) artWrap.style.display = '';
            if (thumbnail) {
                if (artImg)        { artImg.src = thumbnail; artImg.style.display = ''; }
                if (artPlaceholder) artPlaceholder.style.display = 'none';
            } else {
                if (artImg)        { artImg.src = ''; artImg.style.display = 'none'; }
                if (artPlaceholder) artPlaceholder.style.display = '';
            }
        } else {
            if (artWrap) artWrap.style.display = 'none';
        }

        // -- Pause overlay --
        if (pauseOverlay) {
            if (isPlaying) {
                pauseOverlay.classList.remove('np-paused');
            } else {
                pauseOverlay.classList.add('np-paused');
            }
        }

        // -- Progress bar --
        if (showProgress && progress > 0) {
            if (progressWrap) progressWrap.style.display = '';
            if (progressBar)  progressBar.style.width = (progress * 100).toFixed(2) + '%';
        } else {
            if (progressWrap) progressWrap.style.display = 'none';
        }

        // -- Theme hook --
        var onUpdate = themeVal('onUpdate', null);
        if (typeof onUpdate === 'function') onUpdate(payload);

        // -- Visibility --
        if (visibilityMode === 'on_track_change') {
            if (hasNewTrack && title) {
                showWidget();
                var displayMs = themeVal('displayDuration', 8000);
                hideTimer = setTimeout(hideWidget, displayMs);
            } else if (!title) {
                if (hideTimer) { clearTimeout(hideTimer); hideTimer = null; }
                hideWidget();
            }
        } else {
            // 'always' mode
            if (title) {
                showWidget();
            } else {
                hideWidget();
            }
        }
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    window.addEventListener('obsNowPlayingUpdate', function (e) {
        if (e.detail) update(e.detail);
    });

    window.__nowPlaying = {
        update: update,
        init:   update
    };

})();
