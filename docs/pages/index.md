---
layout: default
title: Downspout Plugins
description: Screenshots and short notes for the Downspout VST3 plugin set.
---

<section class="home-hero">
  <p class="kicker">Downspout</p>
  <h1>Transport-aware VST3 tools for generated parts, gated movement, and playable disruption.</h1>
  <p>
    These plugins are all experimental, with various levels of success for their intended purposes.

    Demo - <a href="https://youtu.be/Rd-ACU0JUdo">Jack's Dream</a>
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
