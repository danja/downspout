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
    <li><a href="{{ '/plugins-summary/' | relative_url }}">Plugin reference</a> - ports, MIDI roles and control maps for every plugin, written for automated agents</li>
    <li>Demo - <a href="https://youtu.be/Rd-ACU0JUdo">Jack's Dream</a></li>
    </ul>
    <h4>See also :</h4>
<ul>
    <li><a href="https://danja.github.io/transmission/">Transmission</a> Generative Audio Workstation</li>
      <li><a href="https://danja.github.io/valis">Valis</a> Virtual Analog LLM Integrated System</li>
       <li><a href="https://github.com/danja/flues">Flues</a> earlier LV2 plugins and Web Audio toys</li>
    </ul>
  </p>
</section>

{% assign all_plugins = site.products | sort: "title" %}

<section class="finder" aria-label="Find a plugin">
  <input id="plugin-filter" type="search" placeholder="Filter {{ all_plugins.size }} plugins by name or function (e.g. reverb, drum, granular, launchpad)" aria-label="Filter plugins">
  <button type="button" id="plugin-random">Surprise me</button>
  <span id="plugin-count" class="finder-count" aria-live="polite"></span>
</section>

<nav class="section-nav" aria-label="Plugin groups">
  {% for g in site.data.groups %}
    <a href="#{{ g.id }}">{{ g.title }}</a>
  {% endfor %}
  <a href="#all">A–Z</a>
</nav>

{% comment %} Anchors for the old category links (/#generative etc.) {% endcomment %}
<span id="generative"></span><span id="midi"></span><span id="instrument"></span><span id="processor"></span><span id="launchpad"></span>

{% for g in site.data.groups %}
{% assign members = site.products | where: "group", g.id | sort: "order" %}
<section class="plugin-section" aria-label="{{ g.title }}" id="{{ g.id }}" data-group>
  <h3>{{ g.title }} <span class="group-count">{{ members.size }}</span></h3>
  <p class="section-blurb">{{ g.blurb }}</p>
  <div class="plugin-grid">
    {% for plugin in members %}
      <a class="plugin-card" href="{{ plugin.url | relative_url }}" data-search="{{ plugin.title }} {{ plugin.kind }} {{ plugin.role }} {{ plugin.summary }} {{ plugin.category }} {{ g.title }}">
        <img src="{{ plugin.screenshot | relative_url }}" alt="{{ plugin.title }} plugin interface" loading="lazy">
        <span class="plugin-kind">{{ plugin.kind }}{% if plugin.category == "launchpad" %} · Launchpad{% endif %}</span>
        <strong>{{ plugin.title }}</strong>
        <span>{{ plugin.summary }}</span>
      </a>
    {% endfor %}
  </div>
</section>
{% endfor %}

<section class="plugin-section" id="all" aria-label="All plugins A to Z">
  <h3>All plugins, A–Z</h3>
  <p class="section-blurb">One line each, for when you remember the name but not the group.</p>
  <ul class="az-list">
    {% for plugin in all_plugins %}
      {% assign grp = site.data.groups | where: "id", plugin.group | first %}
      <li data-search="{{ plugin.title }} {{ plugin.role }} {{ plugin.summary }} {{ grp.title }}"><a href="{{ plugin.url | relative_url }}">{{ plugin.title }}</a> <span>{{ plugin.role }} · <a href="#{{ plugin.group }}">{{ grp.title }}</a></span></li>
    {% endfor %}
  </ul>
</section>

<script>
(function () {
  var input = document.getElementById('plugin-filter');
  var count = document.getElementById('plugin-count');
  var cards = [].slice.call(document.querySelectorAll('.plugin-card'));
  var rows = [].slice.call(document.querySelectorAll('.az-list li'));
  var groups = [].slice.call(document.querySelectorAll('[data-group]'));
  function apply() {
    var terms = input.value.toLowerCase().split(/\s+/).filter(Boolean);
    function hit(el) {
      var s = el.getAttribute('data-search').toLowerCase();
      return terms.every(function (t) { return s.indexOf(t) !== -1; });
    }
    var shown = 0;
    cards.forEach(function (c) { var h = hit(c); c.style.display = h ? '' : 'none'; if (h) shown++; });
    rows.forEach(function (r) { r.style.display = hit(r) ? '' : 'none'; });
    groups.forEach(function (g) {
      g.style.display = g.querySelector('.plugin-card:not([style*="none"])') ? '' : 'none';
    });
    count.textContent = terms.length ? shown + ' match' + (shown === 1 ? '' : 'es') : '';
  }
  input.addEventListener('input', apply);
  document.getElementById('plugin-random').addEventListener('click', function () {
    var c = cards[Math.floor(Math.random() * cards.length)];
    window.location.href = c.getAttribute('href');
  });
})();
</script>
