$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$folder = Join-Path $project 'resources/icons/tools'
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$source = Get-Content -LiteralPath (Join-Path $project 'src/ui/canvas/CanvasView.cpp') -Raw
$catalogue = [regex]::Match($source, 'QStringList\s+toolNames\(\)\s*\{\s*return\s*\{([^}]+)').Groups[1].Value
$names = [regex]::Matches($catalogue, '"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
foreach ($name in $names) {
    $geometry = '<rect x="4" y="4" width="14" height="14"/>'
    switch -Regex ($name) {
        '^Move$|Content-Aware Move' { $geometry = '<path d="M11 3v16M3 11h16M8 6l3-3 3 3M8 16l3 3 3-3M6 8l-3 3 3 3M16 8l3 3-3 3"/>'; break }
        'Elliptical Marquee' { $geometry = '<ellipse cx="11" cy="11" rx="7" ry="6" stroke-dasharray="2 2"/>'; break }
        'Marquee|Object Selection|Single Row|Single Column' { $geometry = '<rect x="4" y="4" width="14" height="14" stroke-dasharray="2 2"/>'; break }
        'Lasso' { $geometry = '<path d="M8 16C0 12 3 3 11 4c9 0 9 10 1 12-7 2-7-2-4-3 4-2 5 5 2 7"/>'; break }
        'Crop' { $geometry = '<path d="M6 2v14h14M2 6h14v14M7 15 18 4"/>'; break }
        'Eyedropper|Color Sampler' { $geometry = '<path d="m5 15 9-9 3 3-9 9H4zM12 4l7 7M15 3l5 5"/>'; break }
        'Healing|Patch' { $geometry = '<g transform="rotate(-40 11 11)"><rect x="3" y="7" width="16" height="8" rx="3"/><rect x="8" y="8" width="6" height="6"/><path d="M5 11h.1M17 11h.1"/></g>'; break }
        'Brush|Pencil|Color Replacement|Quick Selection' { $geometry = '<path d="m9 13 8-10 3 3-8 10zM9 12c-6 0-2 6-6 7 8 1 11-3 8-5"/>'; break }
        'Stamp' { $geometry = '<rect x="4" y="15" width="14" height="4" rx="1"/><path d="m6 15 2-4V7c0-5 6-5 6 0v4l2 4"/>'; break }
        'Eraser' { $geometry = '<path d="m3 13 9-9 7 7-8 8H8zM7 9l8 7M11 19h9"/>'; break }
        '^Gradient$' { $geometry = '<defs><linearGradient id="g"><stop stop-color="currentColor"/><stop offset="1" stop-color="currentColor" stop-opacity="0"/></linearGradient></defs><rect x="4" y="5" width="14" height="12" fill="url(#g)"/>'; break }
        'Paint Bucket' { $geometry = '<path d="m3 11 7-7 7 7-7 7zM3 11h14M8 3l5 5M18 12c-4 5-1 8 1 6 2-1 0-4-1-6"/>'; break }
        '^Blur$|Smudge' { $geometry = '<path d="M11 3c-3 5-7 8-6 12 2 7 12 5 12-1 0-4-4-8-6-11z"/>'; break }
        '^Sharpen$|^Triangle$' { $geometry = '<path d="m11 3-8 16h16z"/>'; break }
        'Dodge|Burn|Sponge' { $geometry = '<circle cx="9" cy="9" r="5"/><path d="m12 12 7 7"/>'; break }
        'Pen|Anchor|Convert Point' { $geometry = '<path d="m4 18 3-11 10-4 2 2-4 10zM4 18l8-8M12 18l6-6"/><circle cx="12" cy="10" r="2"/>'; break }
        'Type' { $geometry = '<path d="M4 8V5h14v3M11 5v13M7 18h8"/>'; break }
        'Path Selection|Direct Selection' { $geometry = '<path d="M5 3v15l4-4 3 6 3-2-3-6h6z"/>'; break }
        '^Ellipse$' { $geometry = '<circle cx="11" cy="11" r="7"/>'; break }
        '^Polygon$|Custom Shape' { $geometry = '<path d="m11 3 7 4v8l-7 4-7-4V7z"/>'; break }
        '^Line$' { $geometry = '<path d="m4 18 14-14"/>'; break }
        '^Hand$' { $geometry = '<path d="m7 18-4-7c0-3 3-1 4 0V5c0-2 2-2 2 0v5l1-7c0-2 2-2 2 1v6l1-6c0-2 2-1 2 1v6l1-5c1-2 3-1 2 2v7l-3 5H8z"/>'; break }
        'Rotate View' { $geometry = '<path d="M18 7a7 7 0 1 0-2 10M14 2l5 3-4 3M8 11h6M11 8v6"/>'; break }
        '^Zoom$' { $geometry = '<circle cx="9" cy="9" r="6"/><path d="m13 13 7 7M6 9h6M9 6v6"/>'; break }
        '^Ruler$' { $geometry = '<rect x="3" y="7" width="16" height="8"/><path d="M6 7v4M9 7v4M12 7v4M15 7v4"/>'; break }
        '^Note$' { $geometry = '<path d="M4 3h14v11l-6 6H4zM12 20v-6h6M7 7h8M7 10h8"/>'; break }
        '^Count$' { $geometry = '<path d="M8 3 6 19M16 3l-2 16M3 8h16M3 14h16"/>'; break }
        'Magic Wand' { $geometry = '<path d="M4 19 16 7M13 3v3M18 5h3M17 10l3 3M7 4l2 2"/>'; break }
        '^Red Eye$' { $geometry = '<ellipse cx="11" cy="11" rx="8" ry="5"/><circle cx="11" cy="11" r="3"/>'; break }
        '^Frame$' { $geometry = '<path d="M4 4h14v14H4zM4 4l14 14M18 4 4 18"/>'; break }
        '^Artboard$' { $geometry = '<path d="M6 4h12v14H6zM2 7h8M6 3v8"/>'; break }
        '^Slice' { $geometry = '<path d="m3 18 12-12 4 4-12 12zM15 6l3-3 3 3-3 3M4 3v6M1 6h6"/>'; break }
    }
    $slug = $name.ToLowerInvariant() -replace '[^a-z0-9]+','-'
    $svg = '<svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 22 22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><title>Serika '+$name+'</title>'+$geometry+'</svg>'
    Set-Content -LiteralPath (Join-Path $folder ($slug+'.svg')) -Value $svg -Encoding utf8
}
Write-Output ('Generated '+$names.Count+' original Serika tool icons.')
