---
layout: default 
title: Downspout Plugins
description: Screenshots and short notes for the Downspout VST3 plugin set.
---

<section class="home-hero">
  <p class="kicker">Downspout</p>
  <h2>VST3 tools for generated parts, gated movement, and playable disruption.</h2>
  <p>
    These plugins are all experimental, mostly work in progress, with various levels of success for their intended purposes.
<ul>
<li><a href="https://github.com/danja/downspout/releases">Releases</a></li>
    <li>Demo - <a href="https://youtu.be/Rd-ACU0JUdo">Jack's Dream</a></li>
    </ul>
    <h4>See also :</h4>
<ul>
    <li><a href="https://danja.github.io/transmission/">Transmission</a> Generative Audio Workstation</li>
      <li><a href="https://danja.github.io/valis">Valis</a> Virtual Analog LLM Integrated System</li>
       <li><a href="https://github.com/danja/flues">Flues</a> earlier LV2 plugins and Web Audio toys</li>
    </ul>
  </p>
  <nav class="section-nav" aria-label="Plugin types">
    <a href="#generative">Generative</a>
    <a href="#midi">MIDI</a>
    <a href="#instrument">Instruments</a>
    <a href="#processor">Processors</a>
  </nav>
</section>

<section class="plugin-section" aria-label="Generative plugins" id="generative">
  <h3>Generative</h3>
  <p class="section-blurb">Sources of musical material: melody, bass, drum and harmony generators, plus audio-triggered MIDI.</p>
  <div class="plugin-grid">
    {% assign products = site.products | where: "category", "generative" | sort: "order" %}
    {% for plugin in products %}
      <a class="plugin-card" href="{{ plugin.url | relative_url }}">
        <img src="{{ plugin.screenshot | relative_url }}" alt="{{ plugin.title }} plugin interface">
        <span class="plugin-kind">{{ plugin.kind }}</span>
        <strong>{{ plugin.title }}</strong>
        <span>{{ plugin.summary }}</span>
      </a>
    {% endfor %}
  </div>
</section>

<section class="plugin-section" aria-label="MIDI plugins" id="midi">
  <h3>MIDI</h3>
  <p class="section-blurb">Processors, modulators and utilities for shaping MIDI before it reaches an instrument.</p>
  <div class="plugin-grid">
    {% assign products = site.products | where: "category", "midi" | sort: "order" %}
    {% for plugin in products %}
      <a class="plugin-card" href="{{ plugin.url | relative_url }}">
        <img src="{{ plugin.screenshot | relative_url }}" alt="{{ plugin.title }} plugin interface">
        <span class="plugin-kind">{{ plugin.kind }}</span>
        <strong>{{ plugin.title }}</strong>
        <span>{{ plugin.summary }}</span>
      </a>
    {% endfor %}
  </div>
</section>

<section class="plugin-section" aria-label="Instrument plugins" id="instrument">
  <h3>Instruments</h3>
  <p class="section-blurb">Playable sound sources: synths, samplers and physical models.</p>
  <div class="plugin-grid">
    {% assign products = site.products | where: "category", "instrument" | sort: "order" %}
    {% for plugin in products %}
      <a class="plugin-card" href="{{ plugin.url | relative_url }}">
        <img src="{{ plugin.screenshot | relative_url }}" alt="{{ plugin.title }} plugin interface">
        <span class="plugin-kind">{{ plugin.kind }}</span>
        <strong>{{ plugin.title }}</strong>
        <span>{{ plugin.summary }}</span>
      </a>
    {% endfor %}
  </div>
</section>

<section class="plugin-section" aria-label="Processor plugins" id="processor">
  <h3>Processors</h3>
  <p class="section-blurb">Audio effects, mixers and analyzers for shaping sound.</p>
  <div class="plugin-grid">
    {% assign products = site.products | where: "category", "processor" | sort: "order" %}
    {% for plugin in products %}
      <a class="plugin-card" href="{{ plugin.url | relative_url }}">
        <img src="{{ plugin.screenshot | relative_url }}" alt="{{ plugin.title }} plugin interface">
        <span class="plugin-kind">{{ plugin.kind }}</span>
        <strong>{{ plugin.title }}</strong>
        <span>{{ plugin.summary }}</span>
      </a>
    {% endfor %}
  </div>
</section>
