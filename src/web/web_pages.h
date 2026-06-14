// AUTO-GENERATED page template. See src/web/web.cpp (handleRoot/streamTemplate).
// Redesigned "paper docs" config portal (master-detail layout).
//
// Three PROGMEM blobs:
//   PAGE_HTML  - markup + %TOKEN% placeholders, streamed/substituted by handleRoot().
//   PORTAL_CSS - styles, served verbatim from /portal.css (no tokens, long cache).
//   PORTAL_JS  - interactions, served verbatim from /portal.js (no tokens, long cache).
//
// Keeping CSS/JS on their own cacheable routes leaves PAGE_HTML small so peak
// heap during the token-substituted render stays low on the ESP32-C3.
#pragma once
#include <Arduino.h>

// ============================================================================
//  PAGE_HTML - the only document with %TOKEN% placeholders (resolvePlaceholder).
// ============================================================================
static const char PAGE_HTML[] PROGMEM = R"PAGE(<!doctype html>
<html lang="en" data-accent="green" data-mode="light">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmallOLED - Config Portal v%VER%</title>
<meta name="theme-color" content="#f4f0e7">
<script>(function(){try{var a=localStorage.getItem('soled_accent');if(a)document.documentElement.setAttribute('data-accent',a);var m=localStorage.getItem('soled_mode');if(m){document.documentElement.setAttribute('data-mode',m);var mt=document.querySelector('meta[name=theme-color]');if(mt)mt.setAttribute('content',m==='dark'?'#161512':'#f4f0e7');}}catch(e){}})();</script>
<link rel="stylesheet" href="/portal.css?v=%ASSETVER%">
</head>
<body>
<div class="app">

<header class="topbar">
  <button type="button" class="hamburger" id="navToggle" aria-label="Toggle navigation" aria-expanded="false"><span></span></button>
  <div class="tb-brand">
    <span class="brand-mark" aria-hidden="true"></span>
    <span class="tb-name">SmallOLED</span>
    <span class="tb-ver">v%VER%</span>
  </div>
  <span class="tb-sep" aria-hidden="true"></span>
  <span class="tb-crumb" id="crumb">Clock</span>
  <div class="tb-right">
    <div class="acc-pick" role="group" aria-label="Accent colour">
      <span class="lab">Accent</span>
      <button type="button" class="acc-sw on" data-acc="green" title="Green" aria-label="Green accent"><i></i></button>
      <button type="button" class="acc-sw" data-acc="amber" title="Amber" aria-label="Amber accent"><i></i></button>
    </div>
    <div class="mode-toggle" role="group" aria-label="Colour mode">
      <button type="button" data-mode="light" class="on"><span class="ic"></span>Light</button>
      <button type="button" data-mode="dark"><span class="ic"></span>Dark</button>
    </div>
  </div>
</header>

<div class="workspace">

  <div class="nav-scrim" id="navScrim" aria-hidden="true"></div>
  <aside class="sidebar">
    <nav aria-label="Sections">
      <div class="nav-group">
        <div class="nav-label">Configuration</div>
        <button type="button" class="nav-item active" data-nav="clock">Clock</button>
        <button type="button" class="nav-item" data-nav="display">Display</button>
        <button type="button" class="nav-item" data-nav="layout">Display layout<span class="nv-tag">PC</span></button>
        <button type="button" class="nav-item" data-nav="metrics">Visible metrics<span class="nv-tag">PC</span></button>
      </div>
      <div class="nav-group">
        <div class="nav-label">Network</div>
        <button type="button" class="nav-item" data-nav="network">Network</button>
        <button type="button" class="nav-item" data-nav="timezone">Timezone</button>
      </div>
      <div class="nav-group">
        <div class="nav-label">System</div>
        <button type="button" class="nav-item" data-nav="firmware">Maintenance</button>
      </div>
    </nav>

    <div class="sidebar-spacer"></div>

    <div class="status-block">
      <div class="rail-label">Device status</div>
      <div class="status-readout" id="statusReadout">
        <div class="sr-head">
          <span class="sr-led online" id="srLed"></span>
          <span class="sr-title" id="srTitle">connecting...</span>
        </div>
        <dl class="sr-rows">
          <div class="sr-row"><dt>ip</dt><dd id="srIp">%IP%</dd></div>
          <div class="sr-row"><dt>host</dt><dd><span id="srHost">%V_DEVICENAME%</span>.local</dd></div>
          <div class="sr-row"><dt>uptime</dt><dd id="srUptime">--</dd></div>
          <div class="sr-row"><dt>rssi</dt><dd id="srRssi">--</dd></div>
        </dl>
      </div>
    </div>

    <div class="about">
      <span class="line">SmallOLED PC-Monitor &middot; <b>v%VER%</b></span>
      <a href="https://github.com/Keralots/SmallOLED-PCMonitor" target="_blank" rel="noopener"><span class="gh" aria-hidden="true"></span>github.com/Keralots</a>
    </div>
  </aside>

  <main class="content">
    <div class="content-inner">
      <form id="cfgForm" action="/save" method="POST">

        <!-- CLOCK -->
        <section class="page active" data-page="clock">
          <div class="page-header">
            <h1 class="page-h1">Clock</h1>
            <p class="page-lede">Pick the idle animation shown when your PC is asleep, and how the time and date are formatted.</p>
          </div>

          <div class="card">
            <h2 class="card-title">Idle clock</h2>
            <div class="field">
              <label class="field-label" for="clockStyle">Clock style</label>
              <div class="select-wrap">
                <select name="clockStyle" id="clockStyle">
                  <option value="0" %SEL_CLOCKSTYLE_0%>Mario Animation</option>
                  <option value="1" %SEL_CLOCKSTYLE_1%>Standard Clock</option>
                  <option value="2" %SEL_CLOCKSTYLE_2%>Large Clock</option>
                  <option value="3" %SEL_CLOCKSTYLE_3%>Space Invaders</option>
                  <option value="5" %SEL_CLOCKSTYLE_5%>Arkanoid</option>
                  <option value="6" %SEL_CLOCKSTYLE_6%>Pac-Man Clock</option>
                  <option value="7" %SEL_CLOCKSTYLE_7%>Snake</option>
                  <option value="8" %SEL_CLOCKSTYLE_8%>Tetris</option>
                  <option value="10" %SEL_CLOCKSTYLE_10%>Asteroids</option>
                  <option value="11" %SEL_CLOCKSTYLE_11%>Dino Runner</option>
                  <option value="9" %SEL_CLOCKSTYLE_9%>Cycle All Styles (each 5m)</option>
                </select>
              </div>
            </div>

            <!-- Mario -->
            <div class="subcard" id="marioSettings" style="display:%DSP_CLOCKSTYLE_0%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="marioBounceHeight">Bounce height</label>
                  <div class="range-row">
                    <input type="range" name="marioBounceHeight" id="marioBounceHeight" min="10" max="50" step="5" value="%V_MARIOBOUNCEHEIGHT%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="marioBounceHeight">%F_MARIOBOUNCEHEIGHT%</span>
                  </div>
                  <p class="field-hint">How high digits bounce when Mario hits them. Default 3.5.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="marioBounceSpeed">Fall speed</label>
                  <div class="range-row">
                    <input type="range" name="marioBounceSpeed" id="marioBounceSpeed" min="2" max="15" step="1" value="%V_MARIOBOUNCESPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="marioBounceSpeed">%F_MARIOBOUNCESPEED%</span>
                  </div>
                  <p class="field-hint">How fast digits fall back down. Higher is faster. Default 0.6.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="marioWalkSpeed">Walk speed</label>
                  <div class="range-row">
                    <input type="range" name="marioWalkSpeed" id="marioWalkSpeed" min="15" max="35" step="1" value="%V_MARIOWALKSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="marioWalkSpeed">%F_MARIOWALKSPEED%</span>
                  </div>
                  <p class="field-hint">How fast Mario walks. Higher is faster. Default 2.0.</p>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="marioSmoothAnimation" id="marioSmoothAnimation" %CHK_MARIOSMOOTHANIMATION%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Smooth animation</strong><span class="ct-hint">4-frame walk cycle for a smoother stride. Default off.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="marioIdleEncounters" id="marioIdleEncounters" %CHK_MARIOIDLEENCOUNTERS%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Idle encounters</strong><span class="ct-hint">Goombas and Spinies appear between minute changes for Mario to defeat. Default off.</span></span>
              </label>
              <div id="marioEncFields" style="display:%DSP_MARIOIDLEENCOUNTERS%">
                <div class="grid-2" style="margin-top:14px">
                  <div class="field" style="margin-bottom:0">
                    <label class="field-label" for="marioEncounterFreq">Encounter frequency</label>
                    <div class="select-wrap">
                      <select name="marioEncounterFreq" id="marioEncounterFreq">
                        <option value="0" %SEL_MARIOENCOUNTERFREQ_0%>Rare (25-35s)</option>
                        <option value="1" %SEL_MARIOENCOUNTERFREQ_1%>Normal (15-25s)</option>
                        <option value="2" %SEL_MARIOENCOUNTERFREQ_2%>Frequent (8-15s)</option>
                        <option value="3" %SEL_MARIOENCOUNTERFREQ_3%>Chaotic (2-5s)</option>
                      </select>
                    </div>
                  </div>
                  <div class="field" style="margin-bottom:0">
                    <label class="field-label" for="marioEncounterSpeed">Encounter speed</label>
                    <div class="select-wrap">
                      <select name="marioEncounterSpeed" id="marioEncounterSpeed">
                        <option value="0" %SEL_MARIOENCOUNTERSPEED_0%>Slow</option>
                        <option value="1" %SEL_MARIOENCOUNTERSPEED_1%>Normal</option>
                        <option value="2" %SEL_MARIOENCOUNTERSPEED_2%>Fast</option>
                      </select>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Space Invaders / Ship (styles 3 + 4) -->
            <div class="subcard" id="spaceSettings" style="display:%DSP_CLOCKSTYLE_34%">
              <div class="field">
                <label class="field-label" for="spaceCharacterType">Character type</label>
                <div class="select-wrap">
                  <select name="spaceCharacterType" id="spaceCharacterType">
                    <option value="0" %SEL_SPACECHARACTERTYPE_0%>Space Invader</option>
                    <option value="1" %SEL_SPACECHARACTERTYPE_1%>Space Ship (default)</option>
                  </select>
                </div>
                  <p class="field-hint">The character that patrols and attacks the digits.</p>
              </div>
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="spacePatrolSpeed">Patrol speed</label>
                  <div class="range-row">
                    <input type="range" name="spacePatrolSpeed" id="spacePatrolSpeed" min="2" max="15" step="1" value="%V_SPACEPATROLSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="spacePatrolSpeed">%F_SPACEPATROLSPEED%</span>
                  </div>
                  <p class="field-hint">How fast the character drifts during patrol. Default 0.5.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="spaceAttackSpeed">Attack speed</label>
                  <div class="range-row">
                    <input type="range" name="spaceAttackSpeed" id="spaceAttackSpeed" min="10" max="40" step="5" value="%V_SPACEATTACKSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="spaceAttackSpeed">%F_SPACEATTACKSPEED%</span>
                  </div>
                  <p class="field-hint">How fast it slides to attack position. Default 2.5.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="spaceLaserSpeed">Laser speed</label>
                  <div class="range-row">
                    <input type="range" name="spaceLaserSpeed" id="spaceLaserSpeed" min="20" max="80" step="5" value="%V_SPACELASERSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="spaceLaserSpeed">%F_SPACELASERSPEED%</span>
                  </div>
                  <p class="field-hint">How fast the laser extends downward. Default 4.0.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="spaceExplosionGravity">Explosion intensity</label>
                  <div class="range-row">
                    <input type="range" name="spaceExplosionGravity" id="spaceExplosionGravity" min="3" max="10" step="1" value="%V_SPACEEXPLOSIONGRAVITY%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="spaceExplosionGravity">%F_SPACEEXPLOSIONGRAVITY%</span>
                  </div>
                  <p class="field-hint">Fragment gravity - how fast debris falls. Default 0.5.</p>
                </div>
              </div>
            </div>

            <!-- Arkanoid (style 5) -->
            <div class="subcard" id="pongSettings" style="display:%DSP_CLOCKSTYLE_5%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pongBallSpeed">Ball speed</label>
                  <div class="range-row">
                    <input type="range" name="pongBallSpeed" id="pongBallSpeed" min="16" max="30" step="1" value="%V_PONGBALLSPEED%">
                    <span class="range-val" data-for="pongBallSpeed">%V_PONGBALLSPEED%</span>
                  </div>
                  <p class="field-hint">How fast the ball moves. Default 18.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pongBounceStrength">Bounce strength</label>
                  <div class="range-row">
                    <input type="range" name="pongBounceStrength" id="pongBounceStrength" min="1" max="8" step="1" value="%V_PONGBOUNCESTRENGTH%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="pongBounceStrength">%F_PONGBOUNCESTRENGTH%</span>
                  </div>
                  <p class="field-hint">How much digits wobble when hit. Default 0.3.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pongBounceDamping">Bounce damping</label>
                  <div class="range-row">
                    <input type="range" name="pongBounceDamping" id="pongBounceDamping" min="50" max="95" step="5" value="%V_PONGBOUNCEDAMPING%" data-div="100" data-fixed="2">
                    <span class="range-val" data-for="pongBounceDamping">%F2_PONGBOUNCEDAMPING%</span>
                  </div>
                  <p class="field-hint">How quickly the wobble stops. Default 0.85.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pongPaddleWidth">Paddle width</label>
                  <div class="range-row">
                    <input type="range" name="pongPaddleWidth" id="pongPaddleWidth" min="10" max="40" step="2" value="%V_PONGPADDLEWIDTH%" data-suffix="px">
                    <span class="range-val" data-for="pongPaddleWidth">%V_PONGPADDLEWIDTH%px</span>
                  </div>
                  <p class="field-hint">Paddle size. Narrower is harder. Default 20px.</p>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="pongHorizontalBounce" id="pongHorizontalBounce" %CHK_PONGHORIZONTALBOUNCE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Horizontal digit bounce</strong><span class="ct-hint">Digits bounce sideways when hit from the side. Default on.</span></span>
              </label>
            </div>

            <!-- Pac-Man (style 6) -->
            <div class="subcard" id="pacmanSettings" style="display:%DSP_CLOCKSTYLE_6%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pacmanSpeed">Patrol speed</label>
                  <div class="range-row">
                    <input type="range" name="pacmanSpeed" id="pacmanSpeed" min="5" max="30" step="1" value="%V_PACMANSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="pacmanSpeed">%F_PACMANSPEED%</span>
                  </div>
                  <p class="field-hint">Patrol speed at the bottom. Default 1.0 px/frame.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pacmanEatingSpeed">Eating speed</label>
                  <div class="range-row">
                    <input type="range" name="pacmanEatingSpeed" id="pacmanEatingSpeed" min="10" max="50" step="1" value="%V_PACMANEATINGSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="pacmanEatingSpeed">%F_PACMANEATINGSPEED%</span>
                  </div>
                  <p class="field-hint">How fast Pac-Man eats digits. Default 2.0 px/frame.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pacmanMouthSpeed">Mouth speed</label>
                  <div class="range-row">
                    <input type="range" name="pacmanMouthSpeed" id="pacmanMouthSpeed" min="5" max="20" step="1" value="%V_PACMANMOUTHSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="pacmanMouthSpeed">%F_PACMANMOUTHSPEED%</span>
                  </div>
                  <p class="field-hint">Mouth open/close rate (waka-waka). Default 1.0 Hz.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="pacmanPelletCount">Pellets</label>
                  <div class="range-row">
                    <input type="range" name="pacmanPelletCount" id="pacmanPelletCount" min="0" max="20" step="1" value="%V_PACMANPELLETCOUNT%">
                    <span class="range-val" data-for="pacmanPelletCount">%V_PACMANPELLETCOUNT%</span>
                  </div>
                  <p class="field-hint">Pellets shown during patrol. Default 8.</p>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="pacmanPelletRandomSpacing" id="pacmanPelletRandomSpacing" %CHK_PACMANPELLETRANDOMSPACING%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Randomize pellet spacing</strong><span class="ct-hint">Pellets appear at random positions during patrol. Default on.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="pacmanBounceEnabled" id="pacmanBounceEnabled" %CHK_PACMANBOUNCEENABLED%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Digit bounce</strong><span class="ct-hint">New digits bounce into place after being eaten. Default on.</span></span>
              </label>
            </div>

            <!-- Snake (style 7) -->
            <div class="subcard" id="snakeSettings" style="display:%DSP_CLOCKSTYLE_7%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="snakeSpeed">Speed</label>
                  <div class="range-row">
                    <input type="range" name="snakeSpeed" id="snakeSpeed" min="5" max="30" step="1" value="%V_SNAKESPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="snakeSpeed">%F_SNAKESPEED%</span>
                  </div>
                  <p class="field-hint">How fast the snake slithers. Default 1.2 px/frame.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="snakeLength">Starting length</label>
                  <div class="range-row">
                    <input type="range" name="snakeLength" id="snakeLength" min="4" max="12" step="1" value="%V_SNAKELENGTH%">
                    <span class="range-val" data-for="snakeLength">%V_SNAKELENGTH%</span>
                  </div>
                  <p class="field-hint">Body length at start; grows as it eats. Default 8.</p>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="snakeWallBorder" id="snakeWallBorder" %CHK_SNAKEWALLBORDER%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Arena border</strong><span class="ct-hint">Draw a Nokia-style frame around the playfield. Default off.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="snakeShowDate" id="snakeShowDate" %CHK_SNAKESHOWDATE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Show date</strong><span class="ct-hint">Off gives the snake the whole screen and centres the clock. Default off.</span></span>
              </label>
            </div>

            <!-- Tetris (style 8) -->
            <div class="subcard" id="tetrisSettings" style="display:%DSP_CLOCKSTYLE_8%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="tetrisFallSpeed">Slab drop speed</label>
                  <div class="range-row">
                    <input type="range" name="tetrisFallSpeed" id="tetrisFallSpeed" min="5" max="30" step="1" value="%V_TETRISFALLSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="tetrisFallSpeed">%F_TETRISFALLSPEED%</span>
                  </div>
                  <p class="field-hint">Slab drop-in speed (Drop-in Slabs). Default 1.2.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="tetrisDotSpeed">Dot fall speed</label>
                  <div class="range-row">
                    <input type="range" name="tetrisDotSpeed" id="tetrisDotSpeed" min="5" max="30" step="1" value="%V_TETRISDOTSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="tetrisDotSpeed">%F_TETRISDOTSPEED%</span>
                  </div>
                  <p class="field-hint">Falling-dot speed. Lower is slower. Default 1.2.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="tetrisBlockStyle">Block style</label>
                  <div class="select-wrap">
                    <select name="tetrisBlockStyle" id="tetrisBlockStyle">
                      <option value="0" %SEL_TETRISBLOCKSTYLE_0%>LCD Grid (gaps)</option>
                      <option value="1" %SEL_TETRISBLOCKSTYLE_1%>Solid Blocks</option>
                    </select>
                  </div>
                  <p class="field-hint">Look of the digit blocks. Default LCD Grid.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="tetrisAnimStyle">Change animation</label>
                  <div class="select-wrap">
                    <select name="tetrisAnimStyle" id="tetrisAnimStyle">
                      <option value="0" %SEL_TETRISANIMSTYLE_0%>Drop-in Slabs</option>
                      <option value="1" %SEL_TETRISANIMSTYLE_1%>Falling Dots</option>
                    </select>
                  </div>
                  <p class="field-hint">How a digit rebuilds on change. Default Falling Dots.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="tetrisDotOrder">Dot build order</label>
                  <div class="select-wrap">
                    <select name="tetrisDotOrder" id="tetrisDotOrder">
                      <option value="0" %SEL_TETRISDOTORDER_0%>Bottom-up</option>
                      <option value="1" %SEL_TETRISDOTORDER_1%>Random</option>
                    </select>
                  </div>
                  <p class="field-hint">How dots fill in to form the digit. Default Bottom-up.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="tetrisDatePosition">Date position</label>
                  <div class="select-wrap">
                    <select name="tetrisDatePosition" id="tetrisDatePosition">
                      <option value="0" %SEL_TETRISDATEPOSITION_0%>Top</option>
                      <option value="1" %SEL_TETRISDATEPOSITION_1%>Bottom</option>
                    </select>
                  </div>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="tetrisIdleTumble" id="tetrisIdleTumble" %CHK_TETRISIDLETUMBLE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Block game</strong><span class="ct-hint">Auto-playing Tetris fills the bottom while idle (forces a centred, dateless clock). Default on.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="tetrisSmoothGame" id="tetrisSmoothGame" %CHK_TETRISSMOOTHGAME%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Smooth play</strong><span class="ct-hint">Block game plays near-perfectly so rows stay flat and lines clear cleanly. Default off.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="tetrisDigitBounce" id="tetrisDigitBounce" %CHK_TETRISDIGITBOUNCE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Digit bounce</strong><span class="ct-hint">New digit bounces after it rebuilds. Default on.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="tetrisShowDate" id="tetrisShowDate" %CHK_TETRISSHOWDATE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Show date</strong><span class="ct-hint">Uncheck for a cleaner screen. Default on.</span></span>
              </label>
            </div>

            <!-- Asteroids (style 10) -->
            <div class="subcard" id="asteroidsSettings" style="display:%DSP_CLOCKSTYLE_10%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="asteroidsShipSpeed">Ship speed</label>
                  <div class="range-row">
                    <input type="range" name="asteroidsShipSpeed" id="asteroidsShipSpeed" min="5" max="25" step="1" value="%V_ASTEROIDSSHIPSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="asteroidsShipSpeed">%F_ASTEROIDSSHIPSPEED%</span>
                  </div>
                  <p class="field-hint">Thrust and drift speed of the ship. Default 1.2.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="asteroidsRockSpeed">Asteroid speed</label>
                  <div class="range-row">
                    <input type="range" name="asteroidsRockSpeed" id="asteroidsRockSpeed" min="3" max="20" step="1" value="%V_ASTEROIDSROCKSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="asteroidsRockSpeed">%F_ASTEROIDSROCKSPEED%</span>
                  </div>
                  <p class="field-hint">How fast the rocks drift. Default 0.8.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="asteroidsRockCount">Asteroid count</label>
                  <div class="range-row">
                    <input type="range" name="asteroidsRockCount" id="asteroidsRockCount" min="1" max="4" step="1" value="%V_ASTEROIDSROCKCOUNT%">
                    <span class="range-val" data-for="asteroidsRockCount">%V_ASTEROIDSROCKCOUNT%</span>
                  </div>
                  <p class="field-hint">Wireframe rocks on screen. Default 2.</p>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="asteroidsShowDate" id="asteroidsShowDate" %CHK_ASTEROIDSSHOWDATE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Show date</strong><span class="ct-hint">Off gives the ship the whole screen and centres the clock. Default off.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="asteroidsTransparent" id="asteroidsTransparent" %CHK_ASTEROIDSTRANSPARENT%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Transparent digits</strong><span class="ct-hint">Rocks and ship fly through the digits instead of dodging solid time plates. Default on.</span></span>
              </label>
            </div>

            <!-- Dino Runner (style 11) -->
            <div class="subcard" id="dinoSettings" style="display:%DSP_CLOCKSTYLE_11%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="dinoSpeed">Run speed</label>
                  <div class="range-row">
                    <input type="range" name="dinoSpeed" id="dinoSpeed" min="5" max="30" step="1" value="%V_DINOSPEED%" data-div="10" data-fixed="1">
                    <span class="range-val" data-for="dinoSpeed">%F_DINOSPEED%</span>
                  </div>
                  <p class="field-hint">How fast the world scrolls past. Default 1.2.</p>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="dinoCactusFreq">Cactus frequency</label>
                  <div class="select-wrap">
                    <select name="dinoCactusFreq" id="dinoCactusFreq">
                      <option value="0" %SEL_DINOCACTUSFREQ_0%>Rare</option>
                      <option value="1" %SEL_DINOCACTUSFREQ_1%>Normal</option>
                      <option value="2" %SEL_DINOCACTUSFREQ_2%>Frequent</option>
                    </select>
                  </div>
                  <p class="field-hint">How often a cactus rolls in to jump. Default Normal.</p>
                </div>
              </div>
              <label class="check-row standalone" style="margin-top:16px">
                <input type="checkbox" name="dinoShowClouds" id="dinoShowClouds" %CHK_DINOSHOWCLOUDS%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Clouds</strong><span class="ct-hint">Parallax clouds drifting in the background. Default on.</span></span>
              </label>
              <label class="check-row standalone" style="margin-top:12px">
                <input type="checkbox" name="dinoShowDate" id="dinoShowDate" %CHK_DINOSHOWDATE%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>Show date</strong><span class="ct-hint">Off centres the clock above the runner. Default off.</span></span>
              </label>
            </div>
          </div>

          <div class="card">
            <h2 class="card-title">Time &amp; date</h2>
            <div class="grid-2">
              <div class="field" style="margin-bottom:0">
                <label class="field-label" for="use24Hour">Time format</label>
                <div class="select-wrap">
                  <select name="use24Hour" id="use24Hour">
                    <option value="1" %SEL_USE24HOUR%>24-hour &middot; 14:30</option>
                    <option value="0" %SEL_USE24HOUR_NOT%>12-hour &middot; 2:30 PM</option>
                  </select>
                </div>
              </div>
              <div class="field" style="margin-bottom:0">
                <label class="field-label" for="dateFormat">Date format</label>
                <div class="select-wrap">
                  <select name="dateFormat" id="dateFormat">
                    <option value="0" %SEL_DATEFORMAT_0%>DD/MM/YYYY</option>
                    <option value="1" %SEL_DATEFORMAT_1%>MM/DD/YYYY</option>
                    <option value="2" %SEL_DATEFORMAT_2%>YYYY-MM-DD</option>
                    <option value="3" %SEL_DATEFORMAT_3%>DD.MM.YYYY</option>
                  </select>
                </div>
              </div>
            </div>
          </div>
        </section>

        <!-- DISPLAY -->
        <section class="page" data-page="display">
          <div class="page-header">
            <h1 class="page-h1">Display</h1>
            <p class="page-lede">Tune brightness, the clock colon, refresh behaviour and scheduled night dimming.</p>
          </div>

          <div class="card">
            <h2 class="card-title">Colon &amp; refresh</h2>
            <div class="grid-2">
              <div class="field" style="margin-bottom:0">
                <label class="field-label" for="colonBlinkMode">Clock colon</label>
                <div class="select-wrap">
                  <select name="colonBlinkMode" id="colonBlinkMode">
                    <option value="0" %SEL_COLONBLINKMODE_0%>Solid</option>
                    <option value="1" %SEL_COLONBLINKMODE_1%>Blinking</option>
                    <option value="2" %SEL_COLONBLINKMODE_2%>Off</option>
                  </select>
                </div>
              </div>
              <div class="field" style="margin-bottom:0">
                <label class="field-label" for="colonBlinkRate">Blink rate</label>
                <div class="range-row">
                  <input type="range" name="colonBlinkRate" id="colonBlinkRate" min="5" max="50" step="5" value="%V_COLONBLINKRATE%" data-div="10" data-fixed="1" data-suffix="Hz">
                  <span class="range-val" data-for="colonBlinkRate">%F_COLONBLINKRATE%Hz</span>
                </div>
                  <p class="field-hint">Blink speed. 1.0 Hz is once per second.</p>
              </div>
            </div>
            <div class="field" style="margin:18px 0 0">
              <label class="field-label" for="refreshRateMode">Refresh rate</label>
              <div class="select-wrap">
                <select name="refreshRateMode" id="refreshRateMode">
                  <option value="0" %SEL_REFRESHRATEMODE_0%>Auto (adaptive)</option>
                  <option value="1" %SEL_REFRESHRATEMODE_1%>Manual</option>
                </select>
              </div>
            </div>
            <div class="field" id="refreshRateFields" style="display:%DSP_REFRESHRATEMODE_1%;margin:16px 0 0">
              <label class="field-label" for="refreshRateHz">Manual refresh rate</label>
              <div class="range-row">
                <input type="range" name="refreshRateHz" id="refreshRateHz" min="1" max="60" step="1" value="%V_REFRESHRATEHZ%" data-suffix="Hz">
                <span class="range-val" data-for="refreshRateHz">%V_REFRESHRATEHZ%Hz</span>
              </div>
              <p class="field-hint">Updates per second. Higher is smoother but uses more power.</p>
            </div>
            <div class="note">
              <span class="note-k">auto</span>
              <div>Adaptive mode runs static clocks at <strong>2&nbsp;Hz</strong>, idle animations at <strong>20&nbsp;Hz</strong>, and active scenes up to <strong>40&nbsp;Hz</strong> - a blinking colon roughly doubles OLED lifespan.</div>
            </div>
          </div>

          <div class="card">
            <h2 class="card-title">Brightness</h2>
            <div class="field">
              <label class="field-label" for="displayBrightness">Daytime brightness</label>
              <div class="range-row">
                <input type="range" name="displayBrightness" id="displayBrightness" min="%MINBRIGHT%" max="255" step="5" value="%V_DISPLAYBRIGHTNESS%" data-pct="1">
                <span class="range-val" data-for="displayBrightness">%PCT_DISPLAYBRIGHTNESS%%</span>
              </div>
              <p class="field-hint">%HELP_DISPBRIGHT%</p>
            </div>
            <label class="check-row standalone" style="margin-top:6px">
              <input type="checkbox" name="boostAnim" id="boostAnim" %CHK_BOOSTANIMATIONREFRESH%>
              <span class="check-box" aria-hidden="true"></span>
              <span class="check-text"><strong>Smooth animations</strong><span class="ct-hint">Boost the refresh rate during bounces and explosions, then settle back to save power.</span></span>
            </label>
            %LED_SLIDER%
          </div>

          <div class="card">
            <h2 class="card-title">Night mode</h2>
            <label class="check-row standalone">
              <input type="checkbox" name="enableScheduledDimming" id="enableScheduledDimming" %CHK_ENABLESCHEDULEDDIMMING%>
              <span class="check-box" aria-hidden="true"></span>
              <span class="check-text"><strong>Scheduled dimming</strong><span class="ct-hint">Automatically dim the panel during set hours.</span></span>
            </label>
            <div class="subcard" id="nightFields" style="display:%DSP_ENABLESCHEDULEDDIMMING%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="dimStartHour">Dim from</label>
                  <div class="select-wrap"><select name="dimStartHour" id="dimStartHour">%OPT_DIMSTART%</select></div>
                </div>
                <div class="field" style="margin-bottom:0">
                  <label class="field-label" for="dimEndHour">Until</label>
                  <div class="select-wrap"><select name="dimEndHour" id="dimEndHour">%OPT_DIMEND%</select></div>
                </div>
              </div>
              <div class="field" style="margin:18px 0 0">
                <label class="field-label" for="dimBrightness">Night brightness</label>
                <div class="range-row">
                  <input type="range" name="dimBrightness" id="dimBrightness" min="%MINBRIGHT%" max="255" step="5" value="%V_DIMBRIGHTNESS%" data-pct="1">
                  <span class="range-val" data-for="dimBrightness">%PCT_DIMBRIGHTNESS%%</span>
                </div>
                <p class="field-hint">%HELP_DIMBRIGHT%</p>
              </div>
            </div>
          </div>
        </section>

        <!-- DISPLAY LAYOUT -->
        <section class="page" data-page="layout">
          <div class="page-header">
            <h1 class="page-h1">Display layout</h1>
            <p class="page-lede">How metrics are arranged on the OLED when the PC monitor is streaming. Applies to PC-monitor mode only.</p>
          </div>

          <div class="card">
            <h2 class="card-title">Clock &amp; rows</h2>
            <div class="grid-2">
              <div class="field">
                <label class="field-label" for="clockPosition">Clock position</label>
                <div class="select-wrap">
                  <select name="clockPosition" id="clockPosition">
                    <option value="0" %SEL_CLOCKPOSITION_0%>Center (top)</option>
                    <option value="1" %SEL_CLOCKPOSITION_1%>Left column &middot; row 1</option>
                    <option value="2" %SEL_CLOCKPOSITION_2%>Right column &middot; row 1</option>
                  </select>
                </div>
              </div>
              <div class="field">
                <label class="field-label" for="rowMode">Row mode</label>
                <div class="select-wrap">
                  <select name="rowMode" id="rowMode">
                    <option value="0" %SEL_DISPLAYROWMODE_0%>5 rows &middot; 13px (optimised)</option>
                    <option value="1" %SEL_DISPLAYROWMODE_1%>6 rows &middot; 10px (compact)</option>
                    <option value="2" %SEL_DISPLAYROWMODE_2%>Large 2-row (double size)</option>
                    <option value="3" %SEL_DISPLAYROWMODE_3%>Large 3-row (double size)</option>
                  </select>
                </div>
              </div>
            </div>
            <div class="field">
              <label class="field-label" for="clockOffset">Clock offset (px)</label>
              <input type="number" name="clockOffset" id="clockOffset" value="%V_CLOCKOFFSET%" min="-20" max="20">
              <p class="field-hint">Fine-tune the horizontal clock position, -20 to +20 pixels.</p>
            </div>
            <label class="check-row standalone">
              <input type="checkbox" name="showClock" id="showClock" value="1" %CHK_SHOWCLOCK%>
              <span class="check-box" aria-hidden="true"></span>
              <span class="check-text"><strong>Show clock in metrics view</strong></span>
            </label>
          </div>

          <div class="card">
            <h2 class="card-title">Number formats</h2>
            <div class="check-list">
              <label class="check-row">
                <input type="checkbox" name="rpmKFormat" id="rpmKFormat" %CHK_USERPMKFORMAT%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>K-format for RPM</strong><span class="ct-hint">Show <code>1.8K</code> instead of <code>1800RPM</code> for fans and pumps.</span></span>
              </label>
              <label class="check-row">
                <input type="checkbox" name="netMBFormat" id="netMBFormat" %CHK_USENETWORKMBFORMAT%>
                <span class="check-box" aria-hidden="true"></span>
                <span class="check-text"><strong>M-format for network</strong><span class="ct-hint">Show <code>1.2M</code> instead of <code>1200KB/s</code>.</span></span>
              </label>
            </div>
          </div>
        </section>

        <!-- VISIBLE METRICS -->
        <section class="page" data-page="metrics">
          <div class="page-header">
            <h1 class="page-h1">Visible metrics</h1>
            <p class="page-lede">Assign each metric to a position on the OLED. Applies to PC-monitor mode only.</p>
          </div>

          <div class="card">
            <h2 class="card-title">OLED preview</h2>
            <div class="crt oled-preview">
              <div class="oled-pv-head">
                <span class="ttl">preview</span>
                <span class="meta" id="oledMeta">128x64</span>
              </div>
              <div class="oled-stage" id="oledStage">
                <canvas id="oledCanvas" width="128" height="64"></canvas>
                <div class="drop-cells" id="dropCells"></div>
              </div>
            </div>

            <div class="chip-tray" id="chipTray"><span class="chip-empty">Loading metrics...</span></div>

            <div id="metricsList"></div>

            <div class="note">
              <span class="note-k">tip</span>
              <div><strong>Drag a chip onto the screen</strong> to place it, or tap a chip then tap a slot. <strong>Click a placed metric on the screen to remove it</strong> (or drag its chip off). Press Esc or tap empty tray space to cancel a selection. Open a metric below to set its label, pairing and progress bar. The preview is a pixel-exact 1:1 render of the device and updates live from it. Use <code>^</code> in a label for spacing - <code>CPU^^</code> renders as <code>CPU:&nbsp;&nbsp;45C</code>. Metrics themselves are chosen in the companion app (up to 20).</div>
            </div>
          </div>
        </section>

        <!-- NETWORK -->
        <section class="page" data-page="network">
          <div class="page-header">
            <h1 class="page-h1">Network</h1>
            <p class="page-lede">Device name, mDNS hostname and how the device gets its IP address.</p>
          </div>

          <div class="card">
            <h2 class="card-title">Identity</h2>
            <div class="field" style="margin-bottom:0">
              <label class="field-label" for="deviceName">Device name</label>
              <input type="text" name="deviceName" id="deviceName" value="%V_DEVICENAME%" maxlength="31" pattern="^[a-zA-Z][a-zA-Z0-9-]*$">
              <p class="field-hint">Reachable at <code><span id="hostPreview">%V_DEVICENAME%</span>.local</code>. Letters, numbers and hyphens only.</p>
            </div>
          </div>

          <div class="card">
            <h2 class="card-title">IP address</h2>
            <div class="field">
              <label class="field-label" for="useStaticIP">Address mode</label>
              <div class="select-wrap">
                <select name="useStaticIP" id="useStaticIP">
                  <option value="0" %SEL_USESTATICIP_NOT%>DHCP &middot; automatic</option>
                  <option value="1" %SEL_USESTATICIP%>Static IP</option>
                </select>
              </div>
            </div>
            <div class="subcard" id="staticFields" style="display:%DSP_USESTATICIP%">
              <div class="grid-2">
                <div class="field" style="margin-bottom:0"><label class="field-label" for="staticIP">Static IP</label><input type="text" name="staticIP" id="staticIP" value="%V_STATICIP%" placeholder="192.168.1.100" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"></div>
                <div class="field" style="margin-bottom:0"><label class="field-label" for="gateway">Gateway</label><input type="text" name="gateway" id="gateway" value="%V_GATEWAY%" placeholder="192.168.1.1" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"></div>
                <div class="field" style="margin-bottom:0"><label class="field-label" for="subnet">Subnet mask</label><input type="text" name="subnet" id="subnet" value="%V_SUBNET%" placeholder="255.255.255.0" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"></div>
                <div class="field" style="margin-bottom:0"><label class="field-label" for="dns1">Primary DNS</label><input type="text" name="dns1" id="dns1" value="%V_DNS1%" placeholder="8.8.8.8" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"></div>
                <div class="field" style="margin-bottom:0"><label class="field-label" for="dns2">Secondary DNS</label><input type="text" name="dns2" id="dns2" value="%V_DNS2%" placeholder="8.8.4.4" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"></div>
              </div>
            </div>
            <div class="note warn">
              <span class="note-k">restart</span>
              <div>Switching to a static IP reboots the device. Make sure the address doesn't clash with anything else on your network.</div>
            </div>
            <label class="check-row standalone" style="margin-top:16px">
              <input type="checkbox" name="showIPAtBoot" id="showIPAtBoot" value="1" %CHK_SHOWIPATBOOT%>
              <span class="check-box" aria-hidden="true"></span>
              <span class="check-text"><strong>Show IP at startup</strong><span class="ct-hint">Display the IP address on the OLED for 5 seconds after boot.</span></span>
            </label>
          </div>
        </section>

        <!-- TIMEZONE -->
        <section class="page" data-page="timezone">
          <div class="page-header">
            <h1 class="page-h1">Timezone</h1>
            <p class="page-lede">Sets the clock's region. Daylight-saving transitions are handled automatically.</p>
          </div>
          <div class="card">
            <h2 class="card-title">Region</h2>
            <div class="field" style="margin-bottom:0">
              <label class="field-label" for="timezoneRegion">Timezone region</label>
              <div class="select-wrap">
                <select name="timezoneRegion" id="timezoneRegion">%OPT_TZ%</select>
              </div>
              <p class="field-hint">The system automatically switches between standard and daylight saving time for the selected region.</p>
            </div>
          </div>
        </section>

        <!-- FIRMWARE -->
        <section class="page" data-page="firmware">
          <div class="page-header">
            <h1 class="page-h1">Maintenance</h1>
            <p class="page-lede">Update firmware over the air, back up or restore your configuration, and reset the device.</p>
          </div>

          <div class="card">
            <h2 class="card-title">Update over the air</h2>
            <div class="crt oled-preview" style="max-width:360px">
              <div class="oled-pv-head"><span class="ttl">installed</span><span class="meta">ESP32-C3 &middot; %DISPLAYMODEL%</span></div>
              <dl class="sr-rows" style="position:relative;z-index:1">
                <div class="sr-row"><dt>version</dt><dd>v%VER%</dd></div>
                <div class="sr-row"><dt>built</dt><dd>%BUILT%</dd></div>
                <div class="sr-row"><dt>free heap</dt><dd id="fwHeap">%HEAP% KB</dd></div>
              </dl>
            </div>
            <div class="ota-drop" id="otaDrop">
              <span class="px" style="clip-path:polygon(40% 0,60% 0,60% 45%,85% 45%,50% 85%,15% 45%,40% 45%)"></span>
              <div class="big">Drop a <code>.bin</code> firmware here</div>
              <div class="small">or <button type="button" class="browse" id="otaBrowse">browse for a file</button> - the device reboots automatically when done</div>
              <input type="file" id="otaFile" accept=".bin" hidden>
            </div>
            <div class="ota-progress" id="otaProgress">
              <div class="ota-bar"><i id="otaFill"></i></div>
              <div class="ota-pct" id="otaPct">Uploading... 0%</div>
            </div>
            <div class="note warn">
              <span class="note-k">care</span>
              <div>Don't unplug or close this tab during an update. Flashing the wrong board image can require a USB re-flash to recover.</div>
            </div>
            <div class="note warn" style="%OTAWARNSTYLE%">
              <span class="note-k">partition</span>
              <div>New features (extra clock animations and this redesigned web page) have made the firmware bigger, and it is now close to the size limit for over-the-air updates on older devices. A future update may be <strong>too large to install from this page</strong> until your device is updated to the newer storage layout - a one-time step. <strong>If an update ever fails with a "size" or "no space" error</strong>, don't worry: just <strong>Export your config below first</strong>, then plug the device into a computer and re-flash it once with the <a href="https://keralots.github.io/SmallOLED-PCMonitor/" target="_blank" rel="noopener">browser flasher</a> (this updates the storage layout automatically). After that, updates from this page work normally again and you can Import your backup.</div>
            </div>
          </div>

          <div class="card">
            <h2 class="card-title">Configuration backup</h2>
            <p class="field-hint" style="margin:0 0 14px">Save all settings to a JSON file, or restore them on this or another device.</p>
            <div class="page-actions" style="margin-top:0">
              <button type="button" class="btn" id="exportBtn"><span class="gl"></span> Export config</button>
              <button type="button" class="btn" id="importBtn"><span class="gl"></span> Import config</button>
              <input type="file" id="importFile" accept=".json" hidden>
            </div>
          </div>

          <div class="card">
            <h2 class="card-title">Factory reset</h2>
            <div class="note warn">
              <span class="note-k">danger</span>
              <div>Erases <strong>all settings and WiFi credentials</strong> and restarts into AP setup mode. This cannot be undone - export a backup first.</div>
            </div>
            <div class="page-actions">
              <button type="button" class="btn btn-danger" id="resetBtn">Factory reset</button>
            </div>
          </div>
        </section>

      </form>
    </div>
  </main>
</div>
</div>

<div class="save-bar">
  <div class="save-bar-inner">
    <div class="save-meta clean" id="saveMeta"><span class="dot"></span><span class="txt">All saved</span></div>
    <button type="submit" form="cfgForm" class="btn btn-accent btn-lg" id="saveBtn">Save &amp; apply</button>
  </div>
</div>

<script>window.SOLED={maxRows:%JS_MAXROWS%,isLarge:%JS_ISLARGE%,minBright:%MINBRIGHT%,ver:"%VER%"};</script>
<script src="/portal.js?v=%ASSETVER%"></script>
</body>
</html>
)PAGE";

// ============================================================================
//  PORTAL_CSS - served verbatim from /portal.css (no %TOKEN% substitution).
// ============================================================================
static const char PORTAL_CSS[] PROGMEM = R"CSS(:root{--paper:#f4f0e7;--paper-2:#efe9db;--sidebar:#f1ece0;--card:#fcfaf4;--card-2:#f6f1e6;--inset:#eee7d6;--ink:#24221c;--ink-soft:#3a382f;--mute:#6c675b;--dim:#8a8472;--faint:#a39c89;--line:#e4ddcc;--line-soft:#ece6d7;--line-2:#d4cbb5;--accent:#1f8a5b;--accent-d:#176c47;--accent-l:#2ba169;--accent-soft:rgba(31,138,91,0.12);--accent-line:rgba(31,138,91,0.32);--on-accent:#ffffff;--ok:#1f8a5b;--warn:#b8740d;--err:#c0392b;--crt-bg:#131e18;--crt-fg:#84f3ad;--crt-dim:#4d8a67;--crt-line:#0c140f;--crt-glow:rgba(132,243,173,0.35);--sans:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;--mono:ui-monospace,"SF Mono","JetBrains Mono",Menlo,Consolas,"Liberation Mono",monospace;--r-sm:6px;--r-md:9px;--r-lg:14px;--sidebar-w:248px;--topbar-h:58px;--shadow-card:0 1px 2px rgba(80,65,35,0.05);--shadow-pop:0 14px 40px rgba(40,33,18,0.16)}[data-accent="amber"]{--accent:#b97512;--accent-d:#97600d;--accent-l:#cf8a1f;--accent-soft:rgba(185,117,18,0.13);--accent-line:rgba(185,117,18,0.34);--ok:#1f8a5b;--crt-bg:#1a1209;--crt-fg:#ffcf7a;--crt-dim:#9c7636;--crt-line:#0d0903;--crt-glow:rgba(255,207,122,0.38)}[data-mode="dark"]{--paper:#161512;--paper-2:#1e1c17;--sidebar:#131210;--card:#201e18;--card-2:#1a1813;--inset:#14130e;--ink:#ece7d8;--ink-soft:#d8d2c0;--mute:#aaa28c;--dim:#847c66;--faint:#5f5847;--line:#2c2920;--line-soft:#24211a;--line-2:#403b2c;--accent:#36b478;--accent-d:#44c98a;--accent-l:#2ba169;--accent-soft:rgba(54,180,120,0.16);--accent-line:rgba(54,180,120,0.40);--ok:#36b478;--shadow-card:0 1px 2px rgba(0,0,0,0.35)}[data-mode="dark"][data-accent="amber"]{--accent:#e0a23f;--accent-d:#f0b556;--accent-l:#c98c2f;--accent-soft:rgba(224,162,63,0.16);--accent-line:rgba(224,162,63,0.40);--crt-bg:#1a1209;--crt-fg:#ffcf7a;--crt-dim:#9c7636;--crt-line:#0d0903;--crt-glow:rgba(255,207,122,0.38)}*{box-sizing:border-box}html,body{margin:0;padding:0;background:var(--paper);color:var(--ink);font-family:var(--sans);font-size:15px;line-height:1.55;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;min-height:100vh}body{transition:background 200ms ease,color 200ms ease}a{color:var(--accent-d);text-decoration:none;transition:color 120ms ease}a:hover{color:var(--accent)}::selection{background:var(--accent-soft)}.app{background:var(--paper);min-height:100vh}.topbar{position:sticky;top:0;z-index:50;height:var(--topbar-h);display:flex;align-items:center;gap:14px;padding:0 24px;background:color-mix(in oklab,var(--paper) 90%,transparent);backdrop-filter:blur(10px) saturate(1.05);border-bottom:1px solid var(--line)}.tb-brand{display:flex;align-items:center;gap:10px;min-width:0}.brand-mark{width:28px;height:28px;border-radius:7px;background:var(--accent);display:grid;place-items:center;flex:none;box-shadow:0 0 0 3px var(--accent-soft)}.brand-mark::after{content:"";width:13px;height:13px;background:linear-gradient(#fff 0 0) 0 0 / 5.5px 5.5px no-repeat,linear-gradient(#fff 0 0) 7.5px 7.5px / 5.5px 5.5px no-repeat,linear-gradient(rgba(255,255,255,.55) 0 0) 7.5px 0 / 5.5px 5.5px no-repeat,linear-gradient(rgba(255,255,255,.55) 0 0) 0 7.5px / 5.5px 5.5px no-repeat}.tb-name{font-weight:600;color:var(--ink);letter-spacing:-0.01em;font-size:15px}.tb-ver{font-family:var(--mono);font-size:11px;color:var(--mute);background:var(--paper-2);border:1px solid var(--line);border-radius:999px;padding:2px 8px}.tb-sep{width:1px;height:22px;background:var(--line-2);margin:0 4px}.tb-crumb{font-size:14px;color:var(--mute);font-weight:500}.tb-right{margin-left:auto;display:flex;align-items:center;gap:12px}.hamburger{display:none;width:34px;height:34px;flex:none;padding:0;cursor:pointer;align-items:center;justify-content:center;background:var(--paper-2);border:1px solid var(--line);border-radius:8px;transition:background 120ms ease,border-color 120ms ease}.hamburger:hover{background:var(--card);border-color:var(--line-2)}.hamburger>span,.hamburger>span::before,.hamburger>span::after{content:"";display:block;width:16px;height:1.6px;border-radius:2px;background:var(--ink);transition:transform 180ms ease,opacity 120ms ease}.hamburger>span{position:relative}.hamburger>span::before{position:absolute;left:0;top:-5px}.hamburger>span::after{position:absolute;left:0;top:5px}html.nav-open .hamburger>span{background:transparent}html.nav-open .hamburger>span::before{transform:translateY(5px) rotate(45deg)}html.nav-open .hamburger>span::after{transform:translateY(-5px) rotate(-45deg)}.nav-scrim{display:none}.acc-pick{display:flex;align-items:center;gap:6px}.acc-pick .lab{font-family:var(--mono);font-size:10px;letter-spacing:0.07em;text-transform:uppercase;color:var(--faint);margin-right:2px}.acc-sw{width:22px;height:22px;border-radius:999px;border:2px solid transparent;cursor:pointer;padding:0;background:var(--paper-2);display:grid;place-items:center;transition:border-color 120ms ease,transform 80ms ease}.acc-sw:hover{transform:scale(1.08)}.acc-sw i{width:13px;height:13px;border-radius:999px;display:block}.acc-sw[data-acc="green"] i{background:#1f8a5b}.acc-sw[data-acc="amber"] i{background:#b97512}.acc-sw.on{border-color:var(--accent)}.mode-toggle{display:inline-flex;background:var(--paper-2);border:1px solid var(--line);border-radius:999px;padding:3px;gap:2px}.mode-toggle button{font:inherit;font-family:var(--mono);font-size:11.5px;cursor:pointer;border:0;background:transparent;color:var(--mute);padding:5px 11px;border-radius:999px;display:inline-flex;align-items:center;gap:6px;transition:background 120ms ease,color 120ms ease}.mode-toggle button:hover{color:var(--ink)}.mode-toggle button.on{background:var(--card);color:var(--ink);box-shadow:var(--shadow-card)}.mode-toggle .ic{width:11px;height:11px;border-radius:999px}.mode-toggle button[data-mode="light"] .ic{background:#e0a23f;box-shadow:0 0 0 2px color-mix(in oklab,#e0a23f 30%,transparent)}.mode-toggle button[data-mode="dark"] .ic{background:transparent;box-shadow:inset -3px -1px 0 0 var(--mute)}.workspace{display:grid;grid-template-columns:var(--sidebar-w) minmax(0,1fr);align-items:start;background:var(--paper);min-height:calc(100vh - var(--topbar-h))}.sidebar{position:sticky;top:var(--topbar-h);align-self:start;height:calc(100vh - var(--topbar-h));overflow-y:auto;border-right:1px solid var(--line);background:var(--sidebar);padding:22px 16px 26px;display:flex;flex-direction:column;gap:22px;scrollbar-width:thin;scrollbar-color:var(--line-2) transparent}.sidebar::-webkit-scrollbar{width:6px}.sidebar::-webkit-scrollbar-thumb{background:var(--line-2);border-radius:999px}.nav-group{display:flex;flex-direction:column;gap:2px}.nav-group + .nav-group{margin-top:14px}.nav-label{font-family:var(--mono);font-size:10px;letter-spacing:0.1em;text-transform:uppercase;color:var(--faint);padding:0 10px;margin-bottom:6px}.nav-item{display:flex;align-items:center;gap:9px;width:100%;text-align:left;padding:9px 11px;border-radius:8px;border:0;background:transparent;cursor:pointer;font:inherit;font-size:14px;color:var(--mute);position:relative;transition:background 120ms ease,color 120ms ease}.nav-item:hover{background:var(--paper-2);color:var(--ink)}.nav-item.active{background:var(--card);color:var(--ink);font-weight:600;box-shadow:var(--shadow-card)}.nav-item.active::before{content:"";position:absolute;left:0;top:8px;bottom:8px;width:3px;border-radius:3px;background:var(--accent)}.nav-item .nv-tag{margin-left:auto;font-family:var(--mono);font-size:9.5px;letter-spacing:0.04em;text-transform:uppercase;color:var(--dim);background:var(--paper-2);border:1px solid var(--line);border-radius:999px;padding:1px 6px;font-weight:500}.nav-item.active .nv-tag{background:var(--accent-soft);border-color:var(--accent-line);color:var(--accent-d)}.sidebar-spacer{flex:1 1 auto;min-height:8px}.rail-label{font-family:var(--mono);font-size:10px;letter-spacing:0.09em;text-transform:uppercase;color:var(--faint);margin:0 0 8px 2px}.status-readout{background:var(--crt-bg);border:1px solid var(--crt-line);border-radius:var(--r-md);padding:12px 13px;position:relative;overflow:hidden;box-shadow:inset 0 0 24px rgba(0,0,0,.55),inset 0 0 3px var(--crt-glow);font-family:var(--mono)}.status-readout::after{content:"";position:absolute;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.14) 0 1px,transparent 1px 3px)}.sr-head{display:flex;align-items:center;gap:8px;margin-bottom:10px;position:relative;z-index:1}.sr-led{width:8px;height:8px;border-radius:999px;flex:none;background:var(--crt-fg);box-shadow:0 0 7px var(--crt-glow)}.sr-led.online{animation:led-pulse 2.4s ease-in-out infinite}.sr-led.offline{background:var(--crt-dim);box-shadow:none;animation:none}@keyframes led-pulse{0%,100%{opacity:1}50%{opacity:.45}}@media (prefers-reduced-motion:reduce){.sr-led.online{animation:none}}.sr-title{font-size:10.5px;letter-spacing:0.05em;text-transform:uppercase;color:var(--crt-dim)}.sr-rows{display:flex;flex-direction:column;gap:5px;position:relative;z-index:1;margin:0}.sr-row{display:grid;grid-template-columns:50px 1fr;gap:9px;align-items:baseline;font-size:11.5px}.sr-row dt{color:var(--crt-dim)}.sr-row dd{margin:0;color:var(--crt-fg);text-shadow:0 0 6px var(--crt-glow);word-break:break-word}.about{font-family:var(--mono);font-size:11px;color:var(--faint);display:flex;flex-direction:column;gap:5px;padding:0 2px}.about .line b{color:var(--ink-soft);font-weight:600}.about a{display:inline-flex;align-items:center;gap:6px;color:var(--accent-d)}.about a:hover{color:var(--accent)}.about a .gh{width:11px;height:11px;border-radius:3px;background:var(--accent);flex:none}.content{padding:34px 40px 130px;min-width:0}.content-inner{max-width:860px}.page{display:none}.page.active{display:block}.page-header{margin-bottom:24px}.page-h1{margin:0;font-size:25px;font-weight:600;letter-spacing:-0.02em;color:var(--ink)}.page-lede{margin:7px 0 0;color:var(--mute);font-size:14.5px;max-width:64ch;text-wrap:pretty}.page-actions{display:flex;gap:10px;margin-top:16px;flex-wrap:wrap}.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r-lg);padding:22px 24px;margin-bottom:18px;box-shadow:var(--shadow-card)}.card-title{display:flex;align-items:center;gap:10px;font-size:15px;font-weight:600;color:var(--ink);margin:0 0 18px;letter-spacing:-0.01em}.card-title .tag{font-family:var(--mono);font-size:9.5px;letter-spacing:0.05em;text-transform:uppercase;color:var(--dim);background:var(--paper-2);border:1px solid var(--line);border-radius:999px;padding:2px 8px;font-weight:500}.field{margin-bottom:18px}.field:last-child{margin-bottom:0}.field-label{display:block;font-family:var(--mono);font-size:11px;letter-spacing:0.05em;text-transform:uppercase;color:var(--dim);margin:0 0 8px}.field-hint{color:var(--dim);font-size:12.5px;margin:7px 0 0;max-width:70ch;text-wrap:pretty}.field-hint code,.note code,.field-label code{font-family:var(--mono);font-size:0.9em;background:var(--inset);border:1px solid var(--line);border-radius:5px;padding:1px 6px;color:var(--ink-soft)}.select-wrap{position:relative;max-width:520px}.select-wrap::after{content:"";position:absolute;right:14px;top:50%;width:8px;height:8px;border-right:1.5px solid var(--mute);border-bottom:1.5px solid var(--mute);transform:translateY(-70%) rotate(45deg);pointer-events:none}select,input[type="text"],input[type="number"]{appearance:none;-webkit-appearance:none;width:100%;max-width:520px;background:var(--card);color:var(--ink);border:1px solid var(--line-2);border-radius:var(--r-md);padding:11px 14px;font:inherit;font-size:14px;box-shadow:var(--shadow-card);transition:border-color 120ms ease,box-shadow 120ms ease}select{padding-right:36px;cursor:pointer}select:hover,input[type="text"]:hover,input[type="number"]:hover{border-color:var(--line-2)}select:focus,input:focus{outline:none;border-color:var(--accent-line);box-shadow:0 0 0 3px var(--accent-soft)}input::placeholder{color:var(--faint)}.range-row{display:flex;align-items:center;gap:14px;max-width:520px}input[type="range"]{-webkit-appearance:none;appearance:none;flex:1;height:4px;border-radius:999px;background:var(--line-2);cursor:pointer;margin:12px 0}input[type="range"]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:18px;height:18px;border-radius:50%;background:var(--accent);border:3px solid var(--card);box-shadow:0 1px 4px rgba(0,0,0,.25);cursor:pointer;transition:transform 80ms ease}input[type="range"]::-webkit-slider-thumb:hover{transform:scale(1.12)}input[type="range"]::-moz-range-thumb{width:16px;height:16px;border-radius:50%;background:var(--accent);border:3px solid var(--card);cursor:pointer}.range-val{font-family:var(--mono);font-size:13px;color:var(--accent-d);background:var(--accent-soft);border:1px solid var(--accent-line);border-radius:6px;padding:3px 9px;min-width:56px;text-align:center;flex:none}.check-list{display:flex;flex-direction:column}.check-row{display:flex;align-items:flex-start;gap:12px;cursor:pointer;padding:13px 0;border-top:1px solid var(--line-soft)}.check-row:first-child{border-top:0;padding-top:4px}.check-row.standalone{border-top:0;padding:0}.check-row input[type="checkbox"]{position:absolute;opacity:0;width:0;height:0}.check-box{flex:none;width:20px;height:20px;margin-top:0;border-radius:5px;border:1.5px solid var(--line-2);background:var(--card);display:grid;place-items:center;transition:background 120ms ease,border-color 120ms ease}.check-box::after{content:"";width:9px;height:9px;border-radius:2px;background:var(--on-accent);transform:scale(0);transition:transform 130ms cubic-bezier(.3,1.4,.5,1);clip-path:polygon(0 40%,38% 40%,38% 0,62% 0,62% 40%,100% 40%,100% 64%,62% 64%,62% 100%,38% 100%,38% 64%,0 64%)}.check-row input:checked + .check-box{border-color:var(--accent);background:var(--accent)}.check-row input:checked + .check-box::after{transform:scale(1)}.check-row input:focus-visible + .check-box{box-shadow:0 0 0 3px var(--accent-soft)}.check-text{font-size:14px;color:var(--ink-soft)}.check-text strong{color:var(--ink);font-weight:600}.check-text .ct-hint{display:block;color:var(--dim);font-size:12.5px;margin-top:2px}.subcard{margin-top:16px;padding:16px 18px;border-radius:var(--r-md);background:var(--card-2);border:1px solid var(--line)}.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:16px}@media (max-width:560px){.grid-2{grid-template-columns:1fr}}.divider{border:0;border-top:1px solid var(--line);margin:20px 0}.note{display:flex;gap:11px;align-items:flex-start;max-width:72ch;margin:16px 0 0;padding:12px 14px;background:var(--card-2);border:1px solid var(--line);border-left:2px solid var(--accent);border-radius:var(--r-sm);font-size:13.5px;color:var(--mute);text-wrap:pretty}.note.warn{border-left-color:var(--warn)}.note.plain{border-left-color:var(--line-2)}.note .note-k{flex:none;font-family:var(--mono);font-size:10.5px;letter-spacing:0.05em;text-transform:uppercase;color:var(--accent-d);margin-top:3px}.note.warn .note-k{color:var(--warn)}.note.plain .note-k{color:var(--dim)}.note strong{color:var(--ink);font-weight:600}.btn{font:inherit;font-weight:600;font-size:13.5px;display:inline-flex;align-items:center;gap:8px;padding:9px 15px;border-radius:var(--r-md);cursor:pointer;border:1px solid var(--line-2);background:var(--card);color:var(--ink);transition:background 120ms ease,border-color 120ms ease,filter 120ms ease,transform 80ms ease}.btn:hover{background:var(--card-2)}.btn:active{transform:translateY(1px)}.btn:disabled{opacity:.6;cursor:default}.btn .gl{width:12px;height:12px;flex:none;border-radius:2px;background:var(--accent)}.btn-accent{background:var(--accent);border-color:var(--accent-d);color:var(--on-accent)}.btn-accent .gl{background:var(--on-accent)}.btn-accent:hover{filter:brightness(1.05);background:var(--accent)}.btn-danger{color:var(--err);border-color:color-mix(in oklab,var(--err) 36%,var(--line-2))}.btn-danger:hover{background:color-mix(in oklab,var(--err) 8%,var(--card))}.btn-lg{padding:11px 20px;font-size:14px}.crt{background:var(--crt-bg);border:1px solid var(--crt-line);border-radius:var(--r-md);position:relative;overflow:hidden;font-family:var(--mono);box-shadow:inset 0 0 24px rgba(0,0,0,.55),inset 0 0 3px var(--crt-glow)}.crt::after{content:"";position:absolute;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.14) 0 1px,transparent 1px 3px)}.oled-preview{max-width:100%;margin-bottom:18px;padding:14px 15px}.oled-pv-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px;position:relative;z-index:1}.oled-pv-head .ttl{color:var(--crt-fg);font-size:12.5px;text-shadow:0 0 6px var(--crt-glow)}.oled-pv-head .meta{color:var(--crt-dim);font-size:11px}.oled-stage{position:relative;z-index:1;width:100%;max-width:384px;margin:0 auto;aspect-ratio:128 / 64}.oled-stage canvas{display:block;width:100%;height:100%;image-rendering:pixelated;image-rendering:crisp-edges;background:#060d09;border:1px solid var(--crt-line);border-radius:4px}[data-accent="amber"] .oled-stage canvas{background:#0c0803}.drop-cells{position:absolute;inset:0;pointer-events:none}.drop-cell{position:absolute;box-sizing:border-box;border:1px dashed transparent;border-radius:2px;pointer-events:auto;transition:background 100ms ease,border-color 100ms ease}.oled-stage.dragging .drop-cell{border-color:var(--crt-dim)}.drop-cell.over{border-color:var(--crt-fg);border-style:solid;background:rgba(132,243,173,0.16)}.drop-cell.filled{cursor:pointer}.oled-stage:not(.placing) .drop-cell.filled:hover{border-color:var(--crt-fg);border-style:solid;background:rgba(132,243,173,0.12)}.metric-row{border:1px solid var(--line);border-radius:var(--r-md);background:var(--card-2);margin-bottom:8px}.metric-row:hover{border-color:var(--line-2)}details.metric-row{overflow:hidden}.metric-sum{display:flex;align-items:center;gap:10px;padding:12px 14px;cursor:pointer;list-style:none;user-select:none}.metric-sum::-webkit-details-marker{display:none}.metric-sum .ms-main{display:flex;flex-direction:column;min-width:0;margin-right:auto}.metric-sum .ms-nm{font-weight:600;font-size:14px;color:var(--ink)}.metric-sum .ms-sub{font-family:var(--mono);font-size:11.5px;color:var(--dim);margin-top:2px}.metric-sum .ms-badge{flex:none;padding:2px 9px}.metric-sum .ms-chev{flex:none;width:8px;height:8px;border-right:1.5px solid var(--mute);border-bottom:1.5px solid var(--mute);transform:rotate(45deg);transition:transform 160ms ease}details.metric-row[open] .ms-chev{transform:rotate(-135deg)}.metric-body{padding:0 14px 14px;border-top:1px solid var(--line)}.chip-tray{display:flex;flex-wrap:wrap;gap:7px;margin:2px 0 14px;padding:11px 12px;border:1px solid var(--line);border-radius:var(--r-md);background:var(--card-2);min-height:46px}.chip-tray.placing{border-color:var(--accent-line);border-style:dashed;background:var(--accent-soft)}.chip-empty{font-family:var(--mono);font-size:12px;color:var(--dim)}.chip{display:inline-flex;align-items:center;gap:7px;padding:5px 6px 5px 10px;border:1px solid var(--line-2);border-radius:999px;background:var(--card);cursor:grab;font-size:13px;color:var(--ink-soft);user-select:none;transition:border-color 120ms ease,background 120ms ease,box-shadow 120ms ease}.chip:hover{border-color:var(--accent-line)}.chip.drag-src{opacity:0.45}.chip.sel{border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft);background:var(--accent-soft)}.chip .cn{font-weight:600;color:var(--ink)}.chip .cb,.metric-sum .ms-badge{font-family:var(--mono);font-size:10.5px;letter-spacing:0.03em;color:var(--dim);background:var(--paper-2);border:1px solid var(--line);border-radius:999px}.chip .cb{padding:1px 7px}.chip.placed .cb,details.metric-row[data-placed="1"] .ms-badge{color:var(--accent-d);background:var(--accent-soft);border-color:var(--accent-line)}.oled-stage.placing .drop-cell{border-color:var(--crt-dim)}.metric-adv{display:grid;grid-template-columns:1fr 1fr;gap:10px 14px;margin-top:12px}.metric-adv .field-label{margin-bottom:5px}.metric-adv input,.metric-adv select{font-size:13px;padding:8px 11px}.metric-adv select{padding-right:32px}.metric-adv .full{grid-column:1 / -1}.ota-drop{border:1.5px dashed var(--line-2);border-radius:var(--r-md);padding:26px 20px;text-align:center;background:var(--card-2);transition:border-color 120ms ease,background 120ms ease}.ota-drop.drag{border-color:var(--accent);background:var(--accent-soft)}.ota-drop .px{width:22px;height:22px;margin:0 auto 10px;background:var(--accent)}.ota-drop .big{font-weight:600;color:var(--ink);font-size:15px}.ota-drop .small{color:var(--dim);font-size:13px;margin-top:4px}.ota-drop .browse{color:var(--accent-d);text-decoration:underline;text-underline-offset:2px;cursor:pointer;border:0;background:0;font:inherit}.ota-progress{margin-top:16px;display:none}.ota-progress.show{display:block}.ota-bar{height:10px;border-radius:999px;background:var(--inset);border:1px solid var(--line);overflow:hidden}.ota-bar>i{display:block;height:100%;width:0%;background:var(--accent);transition:width 240ms ease}.ota-pct{font-family:var(--mono);font-size:12px;color:var(--mute);margin-top:7px}.save-bar{position:fixed;left:0;right:0;bottom:0;z-index:40;background:color-mix(in oklab,var(--paper) 88%,transparent);backdrop-filter:blur(10px) saturate(1.1);border-top:1px solid var(--line)}.save-bar-inner{padding:12px 40px 12px calc(var(--sidebar-w) + 40px);display:flex;align-items:center;gap:14px;max-width:100%}.save-meta{font-family:var(--mono);font-size:12px;color:var(--dim);margin-right:auto;display:flex;align-items:center;gap:8px}.save-meta .dot{width:7px;height:7px;border-radius:999px;background:var(--warn);box-shadow:0 0 0 3px color-mix(in oklab,var(--warn) 18%,transparent)}.save-meta.clean .dot{background:var(--ok);box-shadow:0 0 0 3px var(--accent-soft)}@media (max-width:880px){.topbar{padding:0 12px;gap:10px}.hamburger{display:inline-flex}.tb-ver,.tb-sep,.tb-crumb{display:none}.acc-pick .lab{display:none}.workspace{grid-template-columns:1fr}.sidebar{position:fixed;top:var(--topbar-h);left:0;width:min(280px,84vw);height:calc(100vh - var(--topbar-h));height:calc(100dvh - var(--topbar-h));overflow-y:auto;z-index:46;border-right:1px solid var(--line);border-bottom:0;box-shadow:var(--shadow-pop);transform:translateX(-102%);transition:transform 220ms ease}html.nav-open .sidebar{transform:none}.nav-scrim{display:block;position:fixed;left:0;right:0;top:var(--topbar-h);bottom:0;z-index:45;background:rgba(20,16,8,0.42);opacity:0;visibility:hidden;transition:opacity 200ms ease,visibility 200ms ease}html.nav-open .nav-scrim{opacity:1;visibility:visible}.content{padding:24px 18px 130px}.save-bar-inner{padding:11px 16px}.save-meta .txt{display:none}}@media (max-width:560px){.page-h1{font-size:22px}.card{padding:18px 16px}.page-actions .btn{flex:1;justify-content:center}.metric-adv{grid-template-columns:1fr}}@media (max-width:410px){.topbar{gap:8px;padding:0 10px}.tb-brand{gap:7px}.tb-right{gap:8px}.mode-toggle button{padding:5px 9px;font-size:11px}.acc-sw{width:20px;height:20px}}@media (max-width:360px){.tb-name{display:none}.acc-pick{gap:4px}.mode-toggle button{font-size:0;gap:0;padding:6px 8px}})CSS";

// ============================================================================
//  PORTAL_JS - served verbatim from /portal.js (no %TOKEN% substitution).
//  Reads runtime config from window.SOLED (emitted inline in PAGE_HTML).
// ============================================================================
static const char PORTAL_JS[] PROGMEM = R"JS(
(function () {
'use strict';
var $  = function (s, r) { return (r || document).querySelector(s); };
var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
var CFG = window.SOLED || {};
var navToggle = $('#navToggle'), navScrim = $('#navScrim');
function setNav(open) {
document.documentElement.classList.toggle('nav-open', open);
if (navToggle) navToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
}
function closeNav() { setNav(false); }
if (navToggle) navToggle.addEventListener('click', function () { setNav(!document.documentElement.classList.contains('nav-open')); });
if (navScrim) navScrim.addEventListener('click', closeNav);
document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeNav(); });
var navItems = $$('.nav-item');
var pages = $$('.page');
var crumb = $('#crumb');
function showPage(key) {
pages.forEach(function (p) { p.classList.toggle('active', p.dataset.page === key); });
navItems.forEach(function (n) { n.classList.toggle('active', n.dataset.nav === key); });
var active = navItems.filter(function (n) { return n.dataset.nav === key; })[0];
if (active && crumb) crumb.textContent = active.textContent.replace(/PC$/, '').trim();
window.scrollTo(0, 0);
closeNav();
try { localStorage.setItem('soled_section', key); } catch (e) {}
}
navItems.forEach(function (n) { n.addEventListener('click', function () { showPage(n.dataset.nav); }); });
try { var s = localStorage.getItem('soled_section'); if (s && $('[data-page="' + s + '"]')) showPage(s); } catch (e) {}
var accSw = $$('.acc-sw');
function setAccent(acc) {
document.documentElement.setAttribute('data-accent', acc);
accSw.forEach(function (b) { b.classList.toggle('on', b.dataset.acc === acc); });
try { localStorage.setItem('soled_accent', acc); } catch (e) {}
}
accSw.forEach(function (b) { b.addEventListener('click', function () { setAccent(b.dataset.acc); }); });
try { var a = localStorage.getItem('soled_accent'); if (a) setAccent(a); } catch (e) {}
var modeBtns = $$('.mode-toggle button');
function setMode(mode) {
document.documentElement.setAttribute('data-mode', mode);
modeBtns.forEach(function (b) { b.classList.toggle('on', b.dataset.mode === mode); });
var meta = $('meta[name="theme-color"]'); if (meta) meta.setAttribute('content', mode === 'dark' ? '#161512' : '#f4f0e7');
try { localStorage.setItem('soled_mode', mode); } catch (e) {}
}
modeBtns.forEach(function (b) { b.addEventListener('click', function () { setMode(b.dataset.mode); }); });
try { var m = localStorage.getItem('soled_mode'); if (m) setMode(m); } catch (e) {}
var saveMeta = $('#saveMeta');
function markDirty() { if (saveMeta) { saveMeta.classList.remove('clean'); $('.txt', saveMeta).textContent = 'Unsaved changes'; } }
function markClean(txt) { if (saveMeta) { saveMeta.classList.add('clean'); $('.txt', saveMeta).textContent = txt || 'All saved'; } }
var form = $('#cfgForm');
form.addEventListener('input', markDirty);
form.addEventListener('change', markDirty);
function fmtRange(inp) {
var span = $('.range-val[data-for="' + inp.id + '"]');
if (!span) return;
var suf = inp.dataset.suffix || '';
if (inp.dataset.pct) { span.textContent = Math.round((inp.value / 255) * 100) + '%'; return; }
var div = parseFloat(inp.dataset.div || '1');
var fixed = parseInt(inp.dataset.fixed || '0', 10);
span.textContent = (inp.value / div).toFixed(fixed) + suf;
}
$$('input[type="range"]').forEach(function (inp) { fmtRange(inp); inp.addEventListener('input', function () { fmtRange(inp); }); });
function toggle(el, on) { if (el) el.style.display = on ? '' : 'none'; }
var nightChk = $('#enableScheduledDimming');
if (nightChk) { var fn = function () { toggle($('#nightFields'), nightChk.checked); }; nightChk.addEventListener('change', fn); fn(); }
var staticSel = $('#useStaticIP');
if (staticSel) { var fs = function () { toggle($('#staticFields'), staticSel.value === '1'); }; staticSel.addEventListener('change', fs); fs(); }
var refSel = $('#refreshRateMode');
if (refSel) { var fr = function () { toggle($('#refreshRateFields'), refSel.value === '1'); }; refSel.addEventListener('change', fr); fr(); }
var marioEnc = $('#marioIdleEncounters');
if (marioEnc) { var fe = function () { toggle($('#marioEncFields'), marioEnc.checked); }; marioEnc.addEventListener('change', fe); fe(); }
var STYLE_PANELS = { '0':'marioSettings','3':'spaceSettings','4':'spaceSettings','5':'pongSettings','6':'pacmanSettings','7':'snakeSettings','8':'tetrisSettings','10':'asteroidsSettings','11':'dinoSettings' };
var ALL_PANELS = ['marioSettings','spaceSettings','pongSettings','pacmanSettings','snakeSettings','tetrisSettings','asteroidsSettings','dinoSettings'];
var clockStyle = $('#clockStyle');
function syncClockPanels() {
ALL_PANELS.forEach(function (id) { var el = document.getElementById(id); if (el) el.style.display = 'none'; });
var show = STYLE_PANELS[clockStyle.value];
if (show) { var e = document.getElementById(show); if (e) e.style.display = ''; }
}
if (clockStyle) { clockStyle.addEventListener('change', syncClockPanels); syncClockPanels(); }
var dn = $('#deviceName');
if (dn) dn.addEventListener('input', function () {
var v = dn.value.toLowerCase() || 'smalloled';
var hp = $('#hostPreview'); if (hp) hp.textContent = v;
var sh = $('#srHost'); if (sh) sh.textContent = v;
});
var metricsData = [];
var DEVTIME = '12:34';
var MAX_ROWS = CFG.maxRows || 5;
var IS_LARGE = !!CFG.isLarge;
function rowGeom() {
var rm = parseInt($('#rowMode').value, 10);
IS_LARGE = (rm >= 2);
MAX_ROWS = (rm === 0) ? 5 : (rm === 1) ? 6 : (rm === 2) ? 2 : 3;
return { rows: MAX_ROWS, cols: IS_LARGE ? 1 : 2, large: IS_LARGE };
}
function byId(id) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].id === id) return metricsData[i]; return null; }
var FONT = (function () {
var h = "00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800ffe7c3e7ff0018241800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a7f3e1c1c08081c1c3e7f14227f22145f5f005f5f06097f017f006689956a606060606094a2ffa29408047e040810207e201008082a1c08081c2a08081e101010100c1e0c1e0c30383e3830060e3e0e06000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c08040408485454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c4400083641000000770000004136080002010204023c2623263c1ea1a161123a4040207a385454555921555579412254547842215554784020545579400c1e5272123955555559395454545939555454580000457c410002457d420001457c407d1211127df0282528f07c545545002054547c547c0a097f4932494949323a4444443a324a4848303a4141217a3a42402078009da0a07d3d4242423d3d4040403d3c24ff2424487e4943662b2ffc2f2bff0929f620c0887e090320545479410000447d413048484a32384040227a007a0a0a727d0d19317d2629292f28262929292630484d4020380808080808080808382f10c8acba2f102834fa00007b000008142a142222142a14085500550055aa55aa55aaff55ff55ff000000ff00101010ff00141414ff001010ff00ff1010f010f0141414fc001414f700ff0000ff00ff1414f404fc141417101f10101f101f1414141f00101010f0000000001f101010101f10101010f010000000ff101010101010101010ff10000000ff140000ff00ff00001f10170000fc04f414141710171414f404f40000ff00f714141414141414f700f7141414171410101f101f141414f4141010f010f000001f101f0000001f14000000fc140000f010f01010ff10ff141414ff141010101f00000000f010fffffffffff0f0f0f0f0ffffff0000000000ffff0f0f0f0f0f3844443844fc4a4a4a347e02020606027e027e0263554941633844443c04407e201e2006027e020299a5e7a5991c2a492a1c4c7201724c304a4d4d303048784830bc625a463d3e494949007e0101017e2a2a2a2a2a44445f444440514a444040444a51400000ff0103e080ff000008086b6b083612362436060f090f06000018180000001010003040ff0101001f01011e00191d1712003c3c3c3c0000000000";
var a = new Uint8Array(1280);
for (var i = 0; i < 1280; i++) a[i] = parseInt(h.substr(i * 2, 2), 16);
return a;
})();
function FB() { this.buf = new Uint8Array(128 * 64); }
FB.prototype.px = function (x, y, v) { x |= 0; y |= 0; if (x >= 0 && x < 128 && y >= 0 && y < 64) this.buf[y * 128 + x] = v; };
FB.prototype.fillRect = function (x, y, w, h, v) { for (var yy = y; yy < y + h; yy++) for (var xx = x; xx < x + w; xx++) this.px(xx, yy, v); };
FB.prototype.drawRect = function (x, y, w, h, v) { var xx, yy; for (xx = x; xx < x + w; xx++) { this.px(xx, y, v); this.px(xx, y + h - 1, v); } for (yy = y; yy < y + h; yy++) { this.px(x, yy, v); this.px(x + w - 1, yy, v); } };
function drawChar(fb, x, y, ch, size) {
var o = ch.charCodeAt(0); if (o > 255) o = 63;
if (x >= 128 || y >= 64 || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0) return;
var base = o * 5;
for (var col = 0; col < 5; col++) {
var line = FONT[base + col];
for (var row = 0; row < 8; row++) {
if ((line >> row) & 1) {
if (size === 1) fb.px(x + col, y + row, 255);
else fb.fillRect(x + col * size, y + row * size, size, size, 255);
}
}
}
}
function write(fb, x, y, text, size, wrap) {
var cx = x, cy = y;
for (var i = 0; i < text.length; i++) {
var ch = text.charAt(i);
if (ch === '\n') { cx = 0; cy += size * 8; continue; }
if (ch === '\r') continue;
if (wrap && (cx + size * 6) > 128) { cx = 0; cy += size * 8; }
drawChar(fb, cx, cy, ch, size); cx += size * 6;
}
return [cx, cy];
}
function buildMetricText(label, unit, value, rpmK, netMB) {
var dl = (label || '').replace(/\^/g, ' ');
var stripped = dl.replace(/ +$/, '');
var trailing = dl.length - stripped.length; dl = stripped;
if (dl.charAt(dl.length - 1) === '%') dl = dl.slice(0, -1);
var spaces = new Array(Math.min(trailing, 10) + 1).join(' ');
if (rpmK && unit === 'RPM' && value >= 1000) return dl + ':' + spaces + (value / 1000).toFixed(1) + 'K';
if (unit === 'KB/s') { var a = value / 10; if (netMB) return dl + ':' + spaces + (a / 1000).toFixed(1) + 'M'; return dl + ':' + spaces + a.toFixed(1) + unit; }
return dl + ':' + spaces + value + unit;
}
function buildCompanionText(unit, value, netMB) {
if (unit === 'KB/s') { var cv = value / 10; if (netMB) return ' ' + (cv / 1000).toFixed(1) + 'M'; return ' ' + cv.toFixed(1) + unit; }
return ' ' + value + unit;
}
function renderFrame() {
saveFormState();
var fb = new FB();
var rm = parseInt($('#rowMode').value, 10);
var showClock = $('#showClock').checked;
var clockPos = parseInt(($('#clockPosition') || {}).value || '0', 10);
var clockOffset = parseInt(($('#clockOffset') || {}).value || '0', 10) || 0;
var rpmK = $('#rpmKFormat') ? $('#rpmKFormat').checked : false;
var netMB = $('#netMBFormat') ? $('#netMBFormat').checked : false;
var ts = DEVTIME || '12:34';
var isLarge = rm >= 2, textH = isLarge ? 16 : 8;
function slotText(pos) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].position === pos) return metricsData[i]; return null; }
function slotBar(pos) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].barPosition === pos) return metricsData[i]; return null; }
function lblOf(mt) { return (mt.label && mt.label.length) ? mt.label : mt.name; }
function drawBar(x, y, mt, large) {
var ax = x + (mt.barOffsetX || 0), aw = (mt.barWidth || 60);
if (ax >= 128 || ax < 0) return;
if (ax + aw > 128) aw = 128 - ax;
if (aw <= 0) return;
var bmin = mt.barMin | 0, bmax = (mt.barMax == null ? 100 : mt.barMax);
var rng = bmax - bmin; if (rng <= 0) rng = 100;
var dv = (mt.unit === 'KB/s') ? Math.floor(mt.value / 10) : mt.value;
var vir = Math.max(bmin, Math.min(dv, bmax)) - bmin;
var fillW = Math.floor(vir * (aw - 2) / rng);
var barH = large ? 16 : 8;
fb.drawRect(ax, y, aw, barH, 255);
if (fillW > 0) fb.fillRect(ax + 1, y + 1, fillW, barH - 2, 255);
}
function drawText(x, y, mt, size, wrap, large) {
var text = buildMetricText(lblOf(mt), mt.unit, mt.value | 0, rpmK, netMB);
var comp = (mt.companionId > 0) ? byId(mt.companionId) : null;
if (comp && !large) text += buildCompanionText(comp.unit, comp.value | 0, netMB);
var cc = write(fb, x, y, text, size, wrap);
if (comp && large) {
var ct = buildCompanionText(comp.unit, comp.value | 0, netMB).slice(1);
var cxp = 128 - ct.length * 12; if (cxp < cc[0] + 4) cxp = cc[0] + 4;
write(fb, cxp, y, ct, size, wrap);
}
}
if (isLarge) {
var maxRows = (rm === 2) ? 2 : 3;
var startY = (rm === 2) ? 8 : 4;
if (showClock) { write(fb, 48 + clockOffset, 0, ts, 1, false); startY = (rm === 2) ? 12 : 10; }
var rowH = (rm === 2) ? 32 : (showClock ? 18 : 20);
for (var r = 0; r < maxRows; r++) {
var y = startY + r * rowH; if (y + textH > 64) break;
var b = slotBar(r); if (b) { drawBar(0, y, b, true); continue; }
var e = slotText(r); if (e) drawText(0, y, e, 2, false, true);
}
} else {
var COL1 = 0, COL2 = 62, maxRowsN = (rm === 0) ? 5 : 6, sY, rH;
if (rm === 0) { sY = 0; rH = (showClock && clockPos === 0) ? 11 : 13; } else { sY = 2; rH = 10; }
if (showClock) {
if (clockPos === 0) { write(fb, 48 + clockOffset, sY, ts, 1, true); sY += 10; }
else if (clockPos === 1) write(fb, COL1 + clockOffset, sY, ts, 1, true);
else if (clockPos === 2) write(fb, COL2 + clockOffset, sY, ts, 1, true);
}
for (var rr = 0; rr < maxRowsN; rr++) {
var yy = sY + rr * rH; if (yy + 8 > 64) break;
var cols = [[COL1, rr * 2, showClock && clockPos === 1 && rr === 0], [COL2, rr * 2 + 1, showClock && clockPos === 2 && rr === 0]];
for (var ci = 0; ci < 2; ci++) {
var cx = cols[ci][0], pos = cols[ci][1]; if (cols[ci][2]) continue;
var bb = slotBar(pos); if (bb) { drawBar(cx, yy, bb, false); continue; }
var ee = slotText(pos); if (ee) drawText(cx, yy, ee, 1, true, false);
}
}
}
blit(fb);
var g = rowGeom();
$('#oledMeta').textContent = '128x64 - ' + g.rows + ' rows - ' + (g.cols === 1 ? 'single column' : '2 columns');
}
function blit(fb) {
var canvas = $('#oledCanvas'); if (!canvas) return;
var ctx = canvas.getContext('2d');
var amber = document.documentElement.getAttribute('data-accent') === 'amber';
var on = amber ? [255, 207, 122] : [132, 243, 173];
var off = amber ? [12, 8, 3] : [6, 13, 9];
var img = ctx.createImageData(128, 64), d = img.data;
for (var i = 0; i < fb.buf.length; i++) {
var c = fb.buf[i] ? on : off, o = i * 4;
d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
}
ctx.putImageData(img, 0, 0);
}
function rowMaxSlots(rm) { return rm === 0 ? 10 : rm === 1 ? 12 : rm === 2 ? 2 : 3; }
function slotGeom(rm, slot, showClock, clockPos) {
if (slot < 0 || slot >= rowMaxSlots(rm)) return null;
if (rm >= 2) {
var sy, rh;
if (showClock) { if (rm === 2) { sy = 12; rh = 32; } else { sy = 10; rh = 18; } }
else { if (rm === 2) { sy = 8; rh = 32; } else { sy = 4; rh = 20; } }
return { x: 0, y: sy + slot * rh, w: 128, h: rh };
}
var sy2, rh2;
if (rm === 0) { sy2 = 0; rh2 = (showClock && clockPos === 0) ? 11 : 13; } else { sy2 = 2; rh2 = 10; }
if (showClock && clockPos === 0) sy2 += 10;
var row = Math.floor(slot / 2), col = slot % 2;
return { x: col === 0 ? 0 : 62, y: sy2 + row * rh2, w: col === 0 ? 60 : 64, h: rh2 };
}
function clockBlockedSlot(rm, clockPos, showClock) { if (!showClock || rm >= 2 || clockPos === 0) return -1; return clockPos === 1 ? 0 : 1; }
function buildDropCells() {
var host = $('#dropCells'); if (!host) return;
var rm = parseInt($('#rowMode').value, 10);
var showClock = $('#showClock').checked;
var clockPos = parseInt(($('#clockPosition') || {}).value || '0', 10);
host.innerHTML = '';
var n = rowMaxSlots(rm), blocked = clockBlockedSlot(rm, clockPos, showClock);
for (var s = 0; s < n; s++) {
if (s === blocked) continue;
var g = slotGeom(rm, s, showClock, clockPos); if (!g) continue;
var h = Math.min(g.h, 64 - g.y); if (h <= 0) continue;
var cell = document.createElement('div');
var occ = metricsData.filter(function (m) { return m.position === s; })[0];
cell.className = 'drop-cell' + (occ ? ' filled' : ''); cell.dataset.slot = s;
if (occ) cell.title = 'Click to remove ' + occ.name + ' from the screen';
cell.style.left = (g.x / 128 * 100) + '%'; cell.style.top = (g.y / 64 * 100) + '%';
cell.style.width = (g.w / 128 * 100) + '%'; cell.style.height = (h / 64 * 100) + '%';
attachDrop(cell, s);
host.appendChild(cell);
}
}
var DRAG_ID = null;
function setPositions(targetSlot) {
saveFormState();
if (targetSlot !== 255) metricsData.forEach(function (x) { if (x.id !== DRAG_ID && x.position === targetSlot) x.position = 255; });
var mt = byId(DRAG_ID); if (mt) { mt.position = targetSlot; if (targetSlot === 255) mt.barPosition = 255; }
renderMetrics(); renderFrame(); buildDropCells(); buildChipTray(); markDirty();
}
function attachDrop(cell, slot) {
cell.addEventListener('dragover', function (e) { if (DRAG_ID != null) { e.preventDefault(); cell.classList.add('over'); } });
cell.addEventListener('dragleave', function () { cell.classList.remove('over'); });
cell.addEventListener('drop', function (e) { e.preventDefault(); cell.classList.remove('over'); if (DRAG_ID != null) setPositions(slot); });
cell.addEventListener('click', function () {
if (SEL_ID != null) { DRAG_ID = SEL_ID; setSel(null); setPositions(slot); DRAG_ID = null; return; }
var occ = metricsData.filter(function (m) { return m.position === slot; })[0];
if (occ) { DRAG_ID = occ.id; setPositions(255); DRAG_ID = null; }
});
}
var SEL_ID = null;
function setSel(id) {
SEL_ID = id;
var st = $('#oledStage'); if (st) st.classList.toggle('placing', id != null);
var tray = $('#chipTray'); if (tray) tray.classList.toggle('placing', id != null);
buildChipTray();
}
function slotLabel(pos, g) {
if (pos === 255 || pos == null) return 'Hidden';
if (g.large) return 'R' + (pos + 1);
return 'R' + (Math.floor(pos / 2) + 1) + (pos % 2 === 0 ? '·L' : '·R');
}
function buildChipTray() {
var host = $('#chipTray'); if (!host) return;
host.innerHTML = '';
if (!metricsData.length) { host.innerHTML = '<span class="chip-empty">No metrics yet - start the companion app on your PC.</span>'; return; }
var g = rowGeom();
var sorted = metricsData.slice().sort(function (a, b) { return a.displayOrder - b.displayOrder; });
sorted.forEach(function (mt) {
var placed = mt.position !== 255 && mt.position != null;
var chip = document.createElement('div');
chip.className = 'chip' + (placed ? ' placed' : '') + (SEL_ID === mt.id ? ' sel' : '');
chip.innerHTML = '<span class="cn">' + esc(mt.name) + '</span><span class="cb">' + slotLabel(mt.position, g) + '</span>';
makeRowDraggable(chip, mt.id);
chip.addEventListener('click', function () { setSel(SEL_ID === mt.id ? null : mt.id); });
host.appendChild(chip);
});
}
function makeRowDraggable(row, id) {
row.setAttribute('draggable', 'true');
row.addEventListener('dragstart', function (e) {
DRAG_ID = id; row.classList.add('drag-src');
var st = $('#oledStage'); if (st) st.classList.add('dragging');
try { e.dataTransfer.setData('text/plain', String(id)); e.dataTransfer.effectAllowed = 'move'; } catch (_) {}
});
row.addEventListener('dragend', function () {
DRAG_ID = null; row.classList.remove('drag-src');
var st = $('#oledStage'); if (st) st.classList.remove('dragging');
});
}
function setupListDrop() {
var tray = $('#chipTray');
if (tray) {
tray.addEventListener('dragover', function (e) { if (DRAG_ID != null) e.preventDefault(); });
tray.addEventListener('drop', function (e) { if (DRAG_ID != null) { e.preventDefault(); setPositions(255); } });
tray.addEventListener('click', function (e) { if (e.target === tray && SEL_ID != null) setSel(null); });
}
document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && SEL_ID != null) setSel(null); });
var list = $('#metricsList'); if (!list) return;
list.addEventListener('dragover', function (e) { if (DRAG_ID != null) e.preventDefault(); });
list.addEventListener('drop', function (e) { if (DRAG_ID != null) { e.preventDefault(); setPositions(255); } });
}
function saveFormState() {
metricsData.forEach(function (mt) {
var lbl = document.querySelector('input[name="label_' + mt.id + '"]'); if (lbl) mt.label = lbl.value;
var comp = document.getElementById('comp_' + mt.id); if (comp) mt.companionId = parseInt(comp.value, 10);
var bp = document.getElementById('barPos_' + mt.id); if (bp) mt.barPosition = parseInt(bp.value, 10);
var bmin = document.querySelector('input[name="barMin_' + mt.id + '"]'); if (bmin) mt.barMin = parseInt(bmin.value, 10) || 0;
var bmax = document.querySelector('input[name="barMax_' + mt.id + '"]'); if (bmax) mt.barMax = parseInt(bmax.value, 10) || 100;
var bw = document.querySelector('input[name="barWidth_' + mt.id + '"]'); if (bw) mt.barWidth = parseInt(bw.value, 10) || 60;
var bo = document.querySelector('input[name="barOffset_' + mt.id + '"]'); if (bo) mt.barOffsetX = parseInt(bo.value, 10) || 0;
});
}
function onRowMode() {
saveFormState();
var g = rowGeom();
var maxPos = g.large ? g.rows : g.rows * 2;
var hidden = metricsData.filter(function (mt) {
return (mt.position !== 255 && mt.position >= maxPos) || (mt.barPosition !== 255 && mt.barPosition >= maxPos);
});
if (hidden.length > 0) {
var names = hidden.map(function (mt) { return mt.name; }).join(', ');
if (!confirm('Warning: ' + hidden.length + ' metric(s) (' + names + ') will be hidden in this row mode. Continue?')) { return; }
metricsData.forEach(function (mt) {
if (mt.position !== 255 && mt.position >= maxPos) mt.position = 255;
if (mt.barPosition !== 255 && mt.barPosition >= maxPos) mt.barPosition = 255;
});
}
renderMetrics(); buildDropCells(); buildChipTray(); renderFrame();
}
function posOptionsHtml(cur, g, includeNoneLabel) {
var html = '<option value="255">' + (includeNoneLabel || 'None (hidden)') + '</option>';
for (var r = 0; r < g.rows; r++) {
if (g.large) {
html += '<option value="' + r + '"' + (cur === r ? ' selected' : '') + '>Row ' + (r + 1) + '</option>';
} else {
var lp = r * 2, rp = r * 2 + 1;
html += '<option value="' + lp + '"' + (cur === lp ? ' selected' : '') + '>Row ' + (r + 1) + ' &middot; Left</option>';
html += '<option value="' + rp + '"' + (cur === rp ? ' selected' : '') + '>Row ' + (r + 1) + ' &middot; Right</option>';
}
}
return html;
}
function esc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
function renderMetrics() {
var g = rowGeom();
var list = $('#metricsList');
var openIds = {}; Array.prototype.forEach.call(list.querySelectorAll('details.metric-row[open]'), function (d) { openIds[d.dataset.id] = 1; });
list.innerHTML = '';
if (!metricsData.length) { list.innerHTML = '<p class="field-hint">No metrics received yet. Start the companion app on your PC.</p>'; return; }
var sorted = metricsData.slice().sort(function (a, b) { return a.displayOrder - b.displayOrder; });
sorted.forEach(function (mt) {
var compName = mt.companionId > 0 ? (metricsData.filter(function (x) { return x.id === mt.companionId; })[0] || {}).name : null;
var compOpts = '<option value="0">None</option>';
metricsData.forEach(function (x) {
if (x.id !== mt.id) compOpts += '<option value="' + x.id + '"' + (mt.companionId === x.id ? ' selected' : '') + '>' + esc(x.name) + ' (' + esc(x.unit) + ')</option>';
});
var placed = mt.position !== 255 && mt.position != null;
var row = document.createElement('details');
row.className = 'metric-row';
row.dataset.id = mt.id;
row.dataset.placed = placed ? '1' : '0';
if (openIds[mt.id]) row.open = true;
row.innerHTML =
'<summary class="metric-sum"><span class="ms-main"><span class="ms-nm">' + esc(mt.label || mt.name) + '</span>' +
'<span class="ms-sub">' + esc(mt.unit) + (compName ? ' · + ' + esc(compName) : '') + '</span></span>' +
'<span class="ms-badge">' + slotLabel(mt.position, g) + '</span><span class="ms-chev"></span></summary>' +
'<div class="metric-body"><div class="metric-adv">' +
'<div><label class="field-label">Custom label (10 max)</label><input type="text" name="label_' + mt.id + '" value="' + esc(mt.label) + '" maxlength="10" placeholder="' + esc(mt.name) + '"></div>' +
'<div><label class="field-label">Pair with</label><div class="select-wrap"><select id="comp_' + mt.id + '" name="companion_' + mt.id + '">' + compOpts + '</select></div></div>' +
'<div class="full"><label class="field-label">Progress bar position</label><div class="select-wrap"><select id="barPos_' + mt.id + '" name="barPosition_' + mt.id + '">' + posOptionsHtml(mt.barPosition, g, 'None') + '</select></div></div>' +
'<div><label class="field-label">Bar min</label><input type="number" name="barMin_' + mt.id + '" value="' + (mt.barMin || 0) + '"></div>' +
'<div><label class="field-label">Bar max</label><input type="number" name="barMax_' + mt.id + '" value="' + (mt.barMax || 100) + '"></div>' +
'<div><label class="field-label">Bar width (px)</label><input type="number" name="barWidth_' + mt.id + '" value="' + (mt.barWidth || 60) + '" min="10" max="64"></div>' +
'<div><label class="field-label">Bar offset X (px)</label><input type="number" name="barOffset_' + mt.id + '" value="' + (mt.barOffsetX || 0) + '" min="0" max="54"></div>' +
'</div></div>' +
'<input type="hidden" name="order_' + mt.id + '" value="' + mt.displayOrder + '">' +
'<input type="hidden" name="position_' + mt.id + '" value="' + mt.position + '">';
list.appendChild(row);
$('#comp_' + mt.id, row).addEventListener('change', function () { saveFormState(); renderMetrics(); renderFrame(); markDirty(); });
$('#barPos_' + mt.id, row).addEventListener('change', function () { saveFormState(); renderFrame(); markDirty(); });
row.addEventListener('input', function () { saveFormState(); renderFrame(); var nm = $('.ms-nm', row), li = document.querySelector('input[name="label_' + mt.id + '"]'); if (nm && li) nm.textContent = li.value || mt.name; });
});
}
var rowModeSel = $('#rowMode');
if (rowModeSel) rowModeSel.addEventListener('change', onRowMode);
var showClockChk = $('#showClock');
if (showClockChk) showClockChk.addEventListener('change', function () { buildDropCells(); renderFrame(); });
var clockPosSel = $('#clockPosition');
if (clockPosSel) clockPosSel.addEventListener('change', function () { buildDropCells(); renderFrame(); });
['clockOffset', 'rpmKFormat', 'netMBFormat'].forEach(function (id) {
var el = document.getElementById(id); if (!el) return;
el.addEventListener('input', renderFrame); el.addEventListener('change', renderFrame);
});
setupListDrop();
function pollMetrics() {
fetch('/metrics').then(function (r) { return r.json(); }).then(function (data) {
if (data.time) DEVTIME = data.time;
if (data.metrics) data.metrics.forEach(function (d) { var m = byId(d.id); if (m) m.value = d.value; });
renderFrame();
}).catch(function () {});
}
fetch('/metrics').then(function (r) { return r.json(); }).then(function (data) {
if (data.time) DEVTIME = data.time;
if (data.metrics && data.metrics.length) { metricsData = data.metrics; renderMetrics(); buildDropCells(); buildChipTray(); renderFrame(); }
else { $('#metricsList').innerHTML = '<p class="field-hint">No metrics received yet. Start the companion app on your PC.</p>'; buildDropCells(); buildChipTray(); renderFrame(); }
setInterval(pollMetrics, 1500);
}).catch(function () { $('#metricsList').innerHTML = '<p class="field-hint">Could not load metrics from the device.</p>'; });
form.addEventListener('submit', function (e) {
e.preventDefault();
saveFormState();
var btn = $('#saveBtn'); var orig = btn.textContent;
btn.disabled = true; btn.textContent = 'Saving...';
var body = new URLSearchParams(new FormData(form));
fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); })
.then(function (d) {
btn.disabled = false; btn.textContent = orig;
if (d.success) {
markClean('Saved');
if (d.networkChanged) {
alert('Network settings changed. The device is restarting - you may need to reconnect at the new IP address.');
setTimeout(function () { window.location.href = '/'; }, 3000);
}
} else { alert('Error saving settings.'); }
})
.catch(function (err) { btn.disabled = false; btn.textContent = orig; alert('Error saving settings: ' + err); });
});
$('#resetBtn').addEventListener('click', function () {
if (!confirm('Have you exported a backup of your settings?\n\nUse "Export config" first if not.\n\nOK to continue with factory reset, Cancel to go back.')) return;
if (!confirm('ARE YOU SURE?\n\nThis permanently erases ALL settings:\n- WiFi credentials\n- Display & clock config\n- Metric labels & layout\n- Network settings\n\nThe device restarts into AP setup mode. This cannot be undone.')) return;
window.location.href = '/reset';
});
$('#exportBtn').addEventListener('click', function () {
fetch('/api/export').then(function (r) { return r.json(); }).then(function (data) {
var blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
var url = URL.createObjectURL(blob);
var a = document.createElement('a'); a.href = url; a.download = 'smalloled-config.json';
document.body.appendChild(a); a.click(); document.body.removeChild(a); URL.revokeObjectURL(url);
}).catch(function (err) { alert('Error exporting configuration: ' + err); });
});
$('#importBtn').addEventListener('click', function () { $('#importFile').click(); });
$('#importFile').addEventListener('change', function (ev) {
var file = ev.target.files[0]; if (!file) return;
var reader = new FileReader();
reader.onload = function (e) {
var cfg;
try { cfg = JSON.parse(e.target.result); } catch (err) { alert('Invalid configuration file: ' + err); return; }
fetch('/api/import', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cfg) })
.then(function (r) { return r.json(); })
.then(function (d) {
if (d.success) { alert('Configuration imported. Reloading...'); location.reload(); }
else { alert('Error importing configuration: ' + d.message); }
})
.catch(function (err) { alert('Error importing configuration: ' + err); });
};
reader.readAsText(file);
});
var drop = $('#otaDrop'), otaFile = $('#otaFile');
$('#otaBrowse').addEventListener('click', function () { otaFile.click(); });
otaFile.addEventListener('change', function () { if (otaFile.files[0]) doUpload(otaFile.files[0]); });
['dragenter', 'dragover'].forEach(function (ev) { drop.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.add('drag'); }); });
['dragleave', 'drop'].forEach(function (ev) { drop.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.remove('drag'); }); });
drop.addEventListener('drop', function (e) { var f = e.dataTransfer.files[0]; if (f) doUpload(f); });
function doUpload(file) {
if (!file.name || file.name.slice(-4) !== '.bin') { alert('Please select a valid .bin firmware file.'); return; }
var prog = $('#otaProgress'), fill = $('#otaFill'), pct = $('#otaPct');
prog.classList.add('show'); fill.style.width = '0%'; pct.textContent = 'Uploading ' + file.name + '... 0%';
var xhr = new XMLHttpRequest();
xhr.upload.addEventListener('progress', function (e) {
if (e.lengthComputable) { var p = Math.round((e.loaded / e.total) * 100); fill.style.width = p + '%'; pct.textContent = 'Uploading ' + file.name + '... ' + p + '%'; }
});
xhr.addEventListener('load', function () {
if (xhr.status === 200) { fill.style.width = '100%'; pct.textContent = '✓ Written - rebooting device...'; setTimeout(function () { window.location.href = '/'; }, 8000); }
else { pct.textContent = (xhr.responseText || 'Upload failed - please try again.'); }
});
xhr.addEventListener('error', function () { pct.textContent = 'Upload error - please try again.'; });
var fd = new FormData(); fd.append('firmware', file);
xhr.open('POST', '/update'); xhr.send(fd);
}
function fmtUptime(sec) {
var d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
function p2(n) { return (n < 10 ? '0' : '') + n; }
return (d > 0 ? d + 'd ' : '') + p2(h) + ':' + p2(m) + ':' + p2(s);
}
function refreshStatus() {
fetch('/api/info').then(function (r) { return r.json(); }).then(function (d) {
if (d.ip) { var e = $('#srIp'); if (e) e.textContent = d.ip; }
if (d.hostname) { var h = $('#srHost'); if (h) h.textContent = String(d.hostname).replace(/\.local$/, ''); }
if (typeof d.uptime === 'number') { var u = $('#srUptime'); if (u) u.textContent = fmtUptime(d.uptime); }
if (typeof d.rssi === 'number') { var rs = $('#srRssi'); if (rs) rs.textContent = d.rssi + ' dBm'; }
if (typeof d.freeHeap === 'number') { var fh = $('#fwHeap'); if (fh) fh.textContent = (d.freeHeap / 1024).toFixed(1) + ' KB'; }
}).catch(function () {});
fetch('/api/status').then(function (r) { return r.json(); }).then(function (d) {
var led = $('#srLed'), title = $('#srTitle');
if (led) { led.classList.toggle('online', !!d.pcOnline); led.classList.toggle('offline', !d.pcOnline); }
if (title) title.textContent = (d.pcOnline ? 'PC online' : 'PC offline') + ' · ' + (d.mode || 'clock');
}).catch(function () {});
}
refreshStatus();
setInterval(refreshStatus, 5000);
})();
)JS";
