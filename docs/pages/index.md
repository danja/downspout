---
layout: default 
title: Downspout Plugins
description: Screenshots and short notes for the Downspout VST3 plugin set.
---

<section class="home-hero">
  <p class="kicker">Downspout</p>
  <h2>Transport-aware VST3 tools for generated parts, gated movement, and playable disruption.</h2>
  <p>
    These plugins are all experimental, mostly work in progress, with various levels of success for their intended purposes.
<ul>
    <li>Demo - <a href="https://youtu.be/Rd-ACU0JUdo">Jack's Dream</a></li>
    <li>See also - <a href="https://danja.github.io/transmission/">Transmission</a> Generative Audio Workstation</li>
       <li><a href="https://github.com/danja/flues">Flues</a> earlier LV2 plugins and Web Audio toys</li>
    </ul>
  </p>
</section>

<section class="plugin-grid" aria-label="Plugins">
  {% assign products = site.products | sort: "order" %}
  {% for plugin in products %}
    <a class="plugin-card" href="{{ plugin.url | relative_url }}">
      <img src="{{ plugin.screenshot | relative_url }}" alt="{{ plugin.title }} plugin interface">
      <span class="plugin-kind">{{ plugin.kind }}</span>
      <strong>{{ plugin.title }}</strong>
      <span>{{ plugin.summary }}</span>
    </a>
  {% endfor %}
</section>
