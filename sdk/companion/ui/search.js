const $ = (s) => document.querySelector(s);
const form = $("#search-form");
const input = $("#q");
const results = $("#results");
const hero = $("#hero");
const modes = $("#modes");
let mode = "web";

modes.querySelectorAll(".mode").forEach((btn) => {
  btn.onclick = () => {
    modes.querySelectorAll(".mode").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    mode = btn.dataset.mode;
    input.placeholder =
      mode === "imagine" ? "Describe an image to generate…" :
      mode === "videos" ? "Search videos with Grok…" :
      mode === "images" ? "Search images with Grok…" :
      "Search with Grok…";
  };
});

$("#open-sidebar").onclick = () => { window.location.href = "/"; };

form.onsubmit = async (e) => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q) return;
  hero.classList.add("hidden");
  results.classList.remove("hidden");
  results.innerHTML = '<div class="thinking">Grok is searching…</div>';
  try {
    const res = await fetch("/api/search", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ query: q, mode }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || "search failed");
    renderResults(q, data);
  } catch (err) {
    results.innerHTML = `<div class="result-card"><p>Error: ${err.message}</p></div>`;
  }
};

function renderResults(query, data) {
  if (data.mode === "imagine" && data.images?.length) {
    results.innerHTML = `<div class="result-card"><h3>Imagine: ${query}</h3>
      <div class="imagine-grid">${data.images.map((u) =>
        `<img src="${u}" alt="generated">`).join("")}</div></div>`;
    return;
  }
  const text = data.answer || data.text || "";
  const links = (data.links || []).map((l) =>
    `<div class="result-card"><h3><a href="${l.url}" target="_blank">${l.title || l.url}</a></h3>
     <p>${l.snippet || ""}</p></div>`).join("");
  results.innerHTML =
    `<div class="result-card result-body">${escapeHtml(text)}</div>` + links;
}

function escapeHtml(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

const params = new URLSearchParams(location.search);
if (params.get("q")) {
  input.value = params.get("q");
  form.requestSubmit();
}