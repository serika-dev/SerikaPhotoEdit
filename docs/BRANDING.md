# PhotoEdit brand

PhotoEdit adapts the [SerikaMoe brand guidelines](https://serika.moe/brand) and [SerikaPay brand guidelines](https://wallet.serika.dev/brand), reviewed on 9 October 2026. These related products have different accent tokens; PhotoEdit uses SerikaMoe's violet identity with the wallet's quiet plum and lavender surfaces.

## Color and typography

- Brand violet: `#8B5CF6`; deep violet: `#5B21B6`; night purple: `#22074A`.
- Wallet reference surfaces: dark paper `#0E0B15`, surface `#181421`, raised `#221D2E`; light paper `#F5F3FA`, white surfaces, raised `#EEEBF6`.
- Outfit for headings, following SerikaMoe's typography guidance; Onest for compact interface text, following the wallet. Both fonts are embedded and available offline, with their SIL Open Font License notices in `resources/licenses`.
- The existing Darkest, Dark, Light and Lightest settings remain available. Text, selection and focus colors adapt to the selected theme. The image surround and transparency checker remain neutral to support color judgment.

`src/ui/Theme.cpp` defines the palette and loads the widget styles from `resources/themes`. Painted controls inherit the Qt palette so the editor and dialogs follow the same theme. Dense tools, panel layout and shortcuts retain the editing workflow; the home screen has more space and larger type.

## PhotoEdit artwork

The supplied root `logo.png` is the source artwork. It is embedded as `:/serika/logo.png` and displayed with its original aspect ratio and transparency. Windows ICO, Linux PNG and macOS ICNS representations are resized from that same file, without recoloring or redrawing it. The home screen and About dialog use the original embedded image.

The SerikaMoe and SerikaPay product wordmarks are reference material, not PhotoEdit assets. Their streaming and wallet identities are not substituted for the supplied PhotoEdit artwork.

## Bundled font provenance

| File | Source | SHA-256 |
| --- | --- | --- |
| `resources/fonts/Outfit.ttf` | [Google Fonts Outfit](https://github.com/google/fonts/blob/main/ofl/outfit/Outfit%5Bwght%5D.ttf) | `fc7287273e66929776e2ba54f144fe699080bec29f61bf649d70d871468aeade` |
| `resources/fonts/Onest.ttf` | [Google Fonts Onest](https://github.com/google/fonts/blob/main/ofl/onest/Onest%5Bwght%5D.ttf) | `966c5c29b4755da84b6854d5c21dd4eaa2420225d0e9874de602de176d4a9f31` |

The logo's source SHA-256 is `18fdb9349b8e6f3ef3b29b23ccc3cb2f0aa513234d9b9a472c4af82656b51387`.
