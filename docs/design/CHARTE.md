# ACNH Power Pockets — Charte graphique & design de l'interface

> Statut : **proposition v1** (2026-11-11), alignée sur la spec UX [`docs/ux/UX_SPEC.md`](../ux/UX_SPEC.md) v1. La spec fixe les panneaux, les flux et les boutons ; la charte fixe l'apparence : couleurs, typographie, positions, composants, et la recette SDL2 de chacun. Les renvois `UX §x` pointent vers la spec.
>
> Maquettes associées : [`docs/design/mockups/index.html`](mockups/index.html) (galerie), une page 1280×720 par écran, plus la planche [`11-components.html`](mockups/11-components.html).
>
> Périmètre : UI/UX uniquement. Rien ici ne modifie la logique de sauvegarde (crypto, hash, `doSave`, backups, restauration, Doctor, offsets).

## Sommaire
1. [Principes](#1-principes)
2. [Palette](#2-palette)
3. [Typographie](#3-typographie)
4. [Icônes et glyphes](#4-icônes-et-glyphes)
5. [Grille et layout 1280×720](#5-grille-et-layout-1280720)
6. [Composants](#6-composants)
7. [Écrans](#7-écrans)
8. [Contraintes d'implémentation SDL2](#8-contraintes-dimplémentation-sdl2)
9. [Libellés FR ↔ EN et ton](#9-libellés-fr--en-et-ton)
10. [Questions ouvertes](#10-questions-ouvertes)

---

## 1. Principes

- **L'île avant tout** : vert feuille en fond, panneau « papier » crème, barres en bois brun, formes douces. C'est l'identité de l'éditeur actuel ; on la garde et on la structure.
- **Rien n'est écrit sans le dire** : une modification en attente est visible à trois niveaux. La ligne ou la cellule porte un repère ambré, l'onglet un point ambré, et l'en-tête affiche la puce « 4 changes ». Le bouton + (Review) passe en ambre dans le pied d'aide. Les seules actions qui écrivent directement en NAND (restauration, Doctor) sont en rouge avec un badge **WRITES NOW**.
- **La couleur n'est jamais seule** : chaque état coloré est doublé par une forme (repère, badge, icône, texte « was … » ou « old » » « new »).
- **Positions stables** : le pied d'aide montre toujours les mêmes touches au même endroit (UX §2.1). Une touche sans effet est grisée, jamais masquée.
- **Lisible en mode portable** : écran 6,2″ à 720p. Taille minimale du texte 20 px (16 px pour les badges uniquement), contraste ≥ 4,5:1 pour tout texte actif.
- **Tout est un rectangle** : chaque élément se dessine avec `SDL_RenderFillRect`, du texte SDL_ttf et au besoin un PNG. Pas d'ombre floue, pas de dégradé.

## 2. Palette

### 2.1 Tokens

Les couleurs historiques sont conservées : `bg.leaf`, `surface.panel`, `wood`, `error` et `ok`. Le vert de surbrillance `#6AA84F` n'est plus utilisé pour la sélection (voir 2.3).

| Token | Hex | Usage |
|---|---|---|
| `bg.leaf` | `#7AC143` | Fond d'écran (existant). Jamais de texte posé directement dessus. |
| `bg.leafDots` | `#6FB739` | Motif de pois optionnel sur le fond (carrés 8×8 tous les 48 px). |
| `surface.panel` | `#F8F5EC` | Panneau principal, onglet actif, boîtes de dialogue (existant). |
| `surface.alt` | `#F1EBDD` | Zébrage d'une ligne sur deux, cellules de poche, en-têtes de groupe. |
| `surface.sunken` | `#E6DCC3` | Champs, cases vides, aperçu de loadout, panneau Info, bouton neutre. |
| `line.soft` | `#DDD0B4` | Séparateurs 2 px, cadre des cellules vides, pages inactives du pager. |
| `wood` | `#7A5C3E` | En-tête, pied d'aide, cadre des menus et dialogues (existant). |
| `wood.dark` | `#5C4430` | Barre d'onglets, ombre pleine des menus et dialogues. |
| `wood.ink` | `#EADFC8` | Texte secondaire sur bois (« Holding: — », titre de bandeau). |
| `wood.mute` | `#A88C6C` | Libellé d'une touche grisée dans le pied d'aide (volontairement faible). |
| `text.primary` | `#5A4029` | Texte courant sur crème (noms d'items, libellés). |
| `text.secondary` | `#7A5C3E` | Texte d'appoint (IDs, compteurs, « was … »). C'est le brun du texte actuel. |
| `text.muted` | `#8A6E4F` | Aide longue en ≥ 24 px seulement (4,35:1). |
| `ink.strong` | `#3B2A1A` | Texte sur ambre et sur jaune. |
| `select.bg` | `#3F7A2E` | Fond de la ligne, cellule, entrée de menu ou bouton focalisé. |
| `select.ink` | `#F8F5EC` | Texte sur sélection, sur `wood`, sur `tab.idle`, sur `ok` et sur `error`. |
| `tab.idle` | `#8B6B47` | Onglet inactif. |
| `modified` | `#E8A33D` | Repère « modifié non appliqué » : onglet de ligne, numéro de slot, point d'onglet, puce « N changes », touche + mise en avant. |
| `modified.ink` | `#A65F00` | Nouvelle valeur affichée sur crème. |
| `warn` | `#F2C14E` | Toast d'avertissement, icône ⚠. |
| `warn.bg` | `#FCEFC7` | Bandeau d'avertissement du panneau System. |
| `warn.ink` | `#7A4B00` | Texte d'avertissement sur crème ou sur `warn.bg`. |
| `error` | `#C0392B` | Erreurs, actions NAND directes, cadre du dialogue danger (existant). |
| `error.bg` | `#F9E1DD` | Fond du badge WRITES NOW, piste de la jauge « Hold A ». |
| `error.ink` | `#A32F23` | Texte sur `error.bg`. |
| `ok` | `#2E7D32` | Succès, puce « Up to date » (existant). |
| `ok.bg` | `#E3F1E0` | Encadré d'information rassurante. |
| `ok.ink` | `#256B29` | Texte sur `ok.bg`. |
| `disabled.ink` | `#B8AC98` | Texte désactivé, cellule « Empty ». Toujours accompagné d'une raison en clair. |
| `badge.fav` | `#F2B705` | Étoile favori (remplissage), contour `wood.dark`. |
| `badge.single.bg` | `#FCE3C2` | Pastille « 1/slot ». |
| `badge.single.ink` | `#8C3A10` | Texte de la pastille « 1/slot ». |
| `veil` | `#2A1E12` à 60 % (alpha 153) | Voile derrière les dialogues. |
| `veil.light` | `#2A1E12` à 31 % (alpha 80) | Voile derrière les menus d'actions et le stepper. |

### 2.2 Contrastes mesurés (WCAG 2.1)

| Texte | Fond | Ratio | Verdict |
|---|---|---|---|
| `text.primary` #5A4029 | `surface.panel` #F8F5EC | 8.76 | ✔ AAA |
| `text.primary` #5A4029 | `surface.alt` #F1EBDD | 8.03 | ✔ AAA |
| `text.primary` #5A4029 | `surface.sunken` #E6DCC3 | 7.00 | ✔ AAA |
| `text.secondary` #7A5C3E | `surface.panel` #F8F5EC | 5.62 | ✔ AA |
| `text.secondary` #7A5C3E | `surface.alt` #F1EBDD | 5.15 | ✔ AA |
| `text.secondary` #7A5C3E | `surface.sunken` #E6DCC3 | 4.49 | ≈ limite AA : préférer `text.primary` dans les champs |
| `text.muted` #8A6E4F | `surface.panel` #F8F5EC | 4.35 | AA grand texte seulement (≥ 24 px) |
| `select.ink` #F8F5EC | `select.bg` #3F7A2E | 4.77 | ✔ AA |
| `select.ink` #F8F5EC | `wood` #7A5C3E | 5.62 | ✔ AA |
| `wood.ink` #EADFC8 | `wood` #7A5C3E | 4.63 | ✔ AA |
| `wood.ink` #EADFC8 | `wood.dark` #5C4430 | 6.83 | ✔ AA |
| `select.ink` #F8F5EC | `tab.idle` #8B6B47 | 4.49 | ≈ AA (libellés 20 px, acceptable) |
| `wood.dark` #5C4430 | `surface.panel` #F8F5EC | 8.29 | ✔ AAA (onglet actif, keycaps) |
| `modified.ink` #A65F00 | `surface.panel` #F8F5EC | 4.52 | ✔ AA |
| `ink.strong` #3B2A1A | `modified` #E8A33D | 6.36 | ✔ AA |
| `ink.strong` #3B2A1A | `warn` #F2C14E | 8.17 | ✔ AAA |
| `warn.ink` #7A4B00 | `warn.bg` #FCEFC7 | 6.46 | ✔ AA |
| `error` #C0392B | `surface.panel` #F8F5EC | 4.99 | ✔ AA |
| `error.ink` #A32F23 | `error.bg` #F9E1DD | 5.64 | ✔ AA |
| `ok` #2E7D32 | `surface.panel` #F8F5EC | 4.70 | ✔ AA |
| `ok.ink` #256B29 | `ok.bg` #E3F1E0 | 5.58 | ✔ AA |
| `select.ink` #F8F5EC | `error` #C0392B | 4.99 | ✔ AA (toast erreur, bouton danger focalisé) |
| `select.ink` #F8F5EC | `ok` #2E7D32 | 4.70 | ✔ AA (toast succès, puce Up to date) |
| `badge.single.ink` #8C3A10 | `badge.single.bg` #FCE3C2 | 6.20 | ✔ AA |
| `disabled.ink` #B8AC98 | `surface.panel` #F8F5EC | 2.05 | volontaire (WCAG exempte le désactivé) ; raison écrite à côté |
| `wood.dark` #5C4430 | `badge.fav` #F2B705 | 4.97 | contour de l'étoile ✔ |

### 2.3 Corrections par rapport à l'existant

| Constat | Ratio | Décision |
|---|---|---|
| Texte crème sur la surbrillance `#6AA84F` (sélection actuelle) | **2.63 ✘** | La sélection passe à `select.bg #3F7A2E` (4.77). Variante en question ouverte : garder `#6AA84F` avec un texte `ink.strong` (4.78). |
| Texte crème posé sur le fond vert `#7AC143` | **2.02 ✘** | Interdit : tout texte vit dans le panneau ou sur bois. |
| Repère `modified` sur `select.bg` | 2.41 (composant non textuel) | Le repère n'est jamais le seul signal : nouvelle valeur en `modified.ink`, détail « was … » en ligne de statut, compteur global. |
| Couleur du statut déduite du texte (`strstr(g_status, "fail")`) | — | Le type du toast est une enum (ok / info / warn / error), plus une recherche dans la chaîne. |

## 3. Typographie

### 3.1 Police

`assets/font.ttf` = **Varela Round Regular** v3.010 (SIL OFL 1.1), 746 glyphes, latin étendu et accents français complets. Un seul fichier est chargé à plusieurs tailles avec `TTF_OpenFont`, comme aujourd'hui (40/28/24).

Métriques (upem 1000, ascender 918, descender −286, line gap 0) :
- **hauteur de ligne ≈ 1,2 × taille** (`TTF_FontHeight`) ;
- largeur moyenne d'une minuscule ≈ 0,54 × taille ;
- largeur d'un chiffre ≈ 0,63 × taille (chiffres à chasse fixe : les colonnes de nombres s'alignent).

### 3.2 Échelle (5 tailles, ouvertes une fois au démarrage)

| Rôle | Token | Taille | Hauteur `TTF_FontHeight` | Usages |
|---|---|---|---|---|
| Display | `font.display` | 32 px | 39 | Titre de l'app dans l'en-tête |
| Title | `font.title` | 28 px | 34 | Titre de dialogue, valeur du stepper, nom du loadout en aperçu |
| Body | `font.body` | 24 px | 29 | Lignes de liste, noms d'items, valeurs, boutons |
| Small | `font.small` | 20 px | 24 | Onglets, pied d'aide, statut, en-têtes de section, IDs, « Holding: » |
| Micro | `font.micro` | 16 px | 19 | Badges uniquement (« 1/slot », « WRITES NOW ») |

On passe de 3 à 5 polices ouvertes (environ 130 Ko de fichier mappé une fois, plus un cache de glyphes par taille), ce qui reste négligeable en mode application. Si la mémoire devient un sujet, `font.micro` peut être remplacé par `font.small` (voir Questions ouvertes).

### 3.3 Règles

- **Casse** : phrase normale (« Place + next »). Les MAJUSCULES sont réservées aux badges (`WRITES NOW`) et aux en-têtes de groupe (Review, Favorites). SDL_ttf ne gère pas l'espacement des lettres : on accepte le rendu tel quel.
- **Nombres** : format anglais avec virgules de milliers (`99,000`, `999,999,999`), comme le jeu en anglais. Quantités préfixées par `×` (U+00D7, présent) : `×10`. Avant / après : `12,345 » 150,000` (`»` U+00BB, présent).
- **IDs d'items** : hexadécimal 4 chiffres préfixé `0x`, en `font.small` / `text.secondary` (`0x16DB`).
- **Troncature** : mesurer avec `TTF_SizeUTF8`, couper au dernier caractère qui tient, puis ajouter `…` (U+2026, présent). On ne coupe plus à un nombre fixe de caractères comme aujourd'hui (`%-16.16s`). Largeurs maximales :

| Contexte | Largeur max | ≈ caractères à cette taille |
|---|---|---|
| Nom dans une cellule de poche (body 24) | 360 px | 27 (p90 des noms = 27) |
| Nom dans une liste pleine largeur (body 24) | 760 px | 58 (le plus long nom = 51, aucune coupe) |
| Nom dans l'aperçu de loadout (small 20) | 196 px | 18 |
| « Holding: … » dans l'en-tête (small 20) | 520 px | 48 |
| Onglet (small 20) | 152 px | 14 (« Bells & Miles » = 13) |

## 4. Icônes et glyphes

Vérification faite sur `assets/font.ttf` en lisant directement la table `cmap`. Cela répond à la question UX Q1 sur les caractères disponibles.

### 4.1 Disponibles dans la police

`•` U+2022 · `·` U+00B7 · `×` U+00D7 · `…` U+2026 · `—` `–` · `‹` `›` U+2039/203A · `«` `»` · `◊` U+25CA · `∅` U+2205 · `≈` `±` `°` `€` · espace chiffre U+2007 · accents FR (é è ê à ç ô ù É À ’).

### 4.2 Absents : remplacement obligatoire

Les symboles utilisés dans la spec UX (`●`, `✓`, `⚠`, `→`, `★`, `Ⓐ`, `⊕`, `⊖`) **ne sont pas dans la police**. Remplacements :

| Voulu | Statut | Remplacement retenu | Repli sans PNG |
|---|---|---|---|
| ★ (favori) | absent | Icône `star` de `ui_icons.png` | Pastille `FAV` en `font.micro` |
| ● (puce « N changes ») | absent | Carré plein 8×8 en `FillRect` | — |
| ✓ (puce « Up to date ») | absent | Icône `check` | Texte seul « Up to date » |
| ⚠ (avertissement, quantité inhabituelle) | absent | Icône `warn` (triangle jaune) | `!` dans un carré `warn` 20×20 |
| → (avant → après) | absent | `»` de la police | `->` ASCII |
| ◀ ▶ (stepper) | absent | `‹` `›` de la police | — |
| ▲ ▼ | absent | Non utilisés (pagination par pages) | — |
| 🔒 (action bloquée) | absent | Icône `lock` | Texte « Blocked: … » seul (toujours présent) |
| Ⓐ Ⓑ Ⓧ Ⓨ, ⊕ ⊖, touches Switch | absent (pas de PUA Nintendo) | **Keycap dessinée** (6.10) | — |

### 4.3 Planche d'icônes `ui_icons.png` (proposée)

Un seul PNG RGBA de 24 px de haut, une icône tous les 24 px, chargé avec le `stbi_load` déjà utilisé pour `icon.png` (`main.cpp:485`) et découpé par `srcrect`. Copié dans `sdmc:/switch/acnh_editor/` à côté de `font.ttf`. Rendu en 20×20 dans l'interface.

| Index | Nom | Couleurs |
|---|---|---|
| 0 | `star` | `badge.fav` + contour 2 px `wood.dark` |
| 1 | `check` | `select.ink` (sur `ok`) |
| 2 | `warn` | `warn` + « ! » `ink.strong` |
| 3 | `lock` | `text.secondary` (ou `select.ink` sur sélection : deuxième variante à l'index 4) |
| 4 | `lock_light` | `select.ink` |
| 5 | `cross` | `error` (réserve) |

Si le fichier manque, chaque usage retombe sur son repli (colonne de droite du 4.2) : l'app reste utilisable sans le PNG.

## 5. Grille et layout 1280×720

Unité de base : **8 px**. Toutes les positions ci-dessous sont des `SDL_Rect {x, y, w, h}` en coordonnées écran.

```
 0 ┌──────────────────────────────────────────────────────── 1280 ┐
24 │ ┌ HEADER (wood) icône · titre ····· Holding: … · [■ 4 changes]┐│
88 │ ├ TABS (wood.dark) [L] Pockets · Favorites · … · System [R] ──┤│
144│ ├ CONTENT (surface.panel) ───────────────────────────────────┤│
   │ │  152 bandeau de contexte (28)                              ││
   │ │  192 liste : 8 lignes × 44 / grille 10 × 40               ││
   │ │  552 pagination (32)                                       ││
600│ ├ STATUS = détail du focus, ou toast (40) ───────────────────┤│
640│ ├ HELP (wood) (A)… (Y)… (B)…        [L][R] Panel (+)Review (−)Discard ┤│
696│ └────────────────────────────────────────────────────────────┘│
720└─ bg.leaf (marges 32 / 24) ─────────────────────────────────────┘
```

| Zone | Rect | Contenu |
|---|---|---|
| Fond | `{0,0,1280,720}` | `bg.leaf` (+ pois optionnels) |
| Panneau | `{32,24,1216,672}` | `surface.panel` |
| En-tête | `{32,24,1216,64}` | `wood`. `icon.png` 48×48 en `(48,32)`, titre `font.display` `select.ink` en x=112 centré verticalement. À droite (bord x=1232) : la **puce de modifs** (6.6), puis, 24 px à sa gauche et aligné à droite, **« Holding: Nook Miles Ticket ×10 »** (`font.small`, `select.ink` ; « Holding: — » en `wood.ink` si la main est vide) |
| Barre d'onglets | `{32,88,1216,56}` | `wood.dark`. Keycap `L` en `(48,100)` 40×32, keycap `R` en `(1192,100)`. 6 onglets de 176 px de large, pas de 180 px, à partir de x=104. Remplacée par un **bandeau-titre** (`font.small` `wood.ink` en x=56) sur l'écran Review |
| Contenu | `{32,144,1216,456}` | `surface.panel`, marge interne 24 → zone utile `{56,152,1168,440}` |
| Bandeau de contexte | `{56,152,1168,28}` | `font.small` `text.secondary` : résumé à gauche (« 20 slots · 17 used · 3 changed »), aide ou tri à droite |
| Liste | `{56,192,1164,348}` | **8 lignes par page**, 40 px au pas de 44 (`y = 192 + i*44`) |
| Pagination | `{56,552,1164,32}` | Un carré 8×8 par page, centré, pas de 16 (page courante `wood`, autres `line.soft`) + « Page 2/4 » `font.small` aligné à droite |
| Statut | `{32,600,1216,40}` | Séparateur 2 px `line.soft` en haut. Au repos : **détail de l'élément focalisé** (le « volet détail » de UX §2.3), `font.small` `text.secondary` en x=56. Remplacé par un toast pendant 3 s |
| Pied d'aide | `{32,640,1216,56}` | `wood`. Positions fixes (6.10) |

Règles :
- **Pagination** (UX §3.3) : pas de défilement ni d'ascenseur. ↓ sur la dernière ligne passe à la page suivante. 8 lignes par page partout (Favoris : 64 max = 8 pages). Catalogue : la ligne Recherche occupe la 1re ligne, il reste 7 résultats par page.
- **Grille des poches** : exception au pas de 44, 10 rangées au pas de 40 (`y = 188 + r*40`, cellules de 36 px de haut), pas de pagination.
- **Ligne double** (Bells & Miles, Loadouts, System) : hauteur 64, pas de 68 (`y = 192 + i*68`, ou 204 sous un bandeau d'avertissement), 6 lignes au maximum.
- **Panneaux à deux colonnes** (Loadouts, System) : liste à gauche (largeur 544 à 600), panneau `surface.sunken` à droite, chanfrein 8, jusqu'à x=1224.
- **Compteur global** : visible sur tous les écrans, y compris sous le voile des dialogues.

## 6. Composants

Chaque composant donne ses **états**, ses **mesures** et une **recette SDL**. `fill(r, c)` signifie `SDL_SetRenderDrawColor` + `SDL_RenderFillRect` ; `text(f, s, c, x, y)` signifie une texture SDL_ttf mise en cache. Les noms suivent le glossaire UX §1.3.

### 6.1 Coins arrondis (primitive commune)
- **Recette « chanfrein »** (par défaut, r ≤ 4) : `fill({x+r, y, w-2r, h})` puis `fill({x, y+r, w, h-2r})`. Deux rectangles donnent un coin coupé à 45° qui se lit comme arrondi à cette taille.
- **r = 8** (menus, dialogues, panneaux d'aperçu) : trois rectangles en escalier ou `SDL_RenderGeometry` (option, voir 8).
- Les maquettes HTML utilisent `border-radius: 4px` / `8px` pour figurer ce rendu.

### 6.2 Barre d'onglets
| État | Rect | Remplissage | Texte |
|---|---|---|---|
| Inactif | `{104+i*180, 96, 176, 48}` | `tab.idle`, chanfrein 4 en haut | `font.small` `select.ink`, centré |
| Actif | `{104+i*180, 92, 176, 52}` | `surface.panel` (se fond dans le contenu) | `font.small` `wood.dark` |
| Avec modifs | + carré 8×8 `modified` à 12 px à droite du libellé | — | — |
| Indisponible (save non montée) | inactif | `tab.idle` | `disabled.ink` |

Libellés : **Pockets · Favorites · Catalog · Bells & Miles · Loadouts · System** (UX §2.2). **Pas de bouclage** (UX §3.3) : sur Pockets la keycap L est grisée, sur System la keycap R est grisée (style « off » de 6.10). Pendant une modale, L et R sont grisés dans le pied d'aide.

### 6.3 Ligne de liste
Rect `{56, 192+i*44, 1164, 40}`, chanfrein 4. Texte principal `font.body` en x=80 (x=112 si une colonne ★ est réservée), centré verticalement (`y + (40-29)/2`). ID ou détail à droite en `font.small`, bord droit x=1200.

| État | Fond | Texte | Repère |
|---|---|---|---|
| Normale | transparent (zébrage `surface.alt` une ligne sur deux) | `text.primary` | — |
| Focus | `select.bg` | `select.ink` (tout le texte de la ligne, IDs compris) | — |
| Modifiée | normale | nouvelle valeur `modified.ink` | **onglet ambré** `{56, y, 8, 40}` `modified` |
| Modifiée + focus | `select.bg` | `select.ink` | onglet ambré conservé |
| Désactivée | transparent | `disabled.ink` + raison (« Quantity (slot is empty) ») | — |
| Danger (System) | normale | libellé en `error` + badge WRITES NOW | — |
| Bloquée (danger pendant des modifs) | normale | libellé `error`, sous-ligne `warn.ink` « Blocked: apply or discard the N pending changes first » | icône `lock` à droite |
| En-tête de groupe | `surface.alt` 32 px | `font.small` MAJUSCULES `text.secondary` + compteur (« POCKETS · 3 ») | — |

### 6.4 Valeur numérique (Bells & Miles)
Ligne double (64 / pas 68).
- Libellé `font.body` `text.primary` en x=88, sous-ligne `font.small` `text.secondary` avec les bornes (« 0 – 9,999,999 »).
- Valeur `font.body` alignée à droite (bord x=1196). Si modifiée : `12,345 » 150,000` en `modified.ink` et onglet ambré.
- Au-delà du maximum du jeu (Wallet > 99,999, question UX Q4) : sous-ligne en `warn.ink` avec l'icône `warn` après le libellé : « Above the game's wallet max (99,999) ».
- L'édition se fait dans le **stepper** (6.14), ouvert par Y ou A.

### 6.5 Grille des 20 slots (2×10)
- Colonnes : `x = 56` et `x = 648`, largeur 576. Rangées : `y = 188 + r*40`, hauteur 36. Slots 1-10 à gauche, 11-20 à droite (ordre actuel, UX §2.3).
- Cellule : fond `surface.alt`, chanfrein 4.
  - **Pastille de numéro** `{x, y, 44, 36}` en `surface.sunken`, numéro `font.small` `text.secondary` centré.
  - Nom `font.body` `text.primary` en x+56, tronqué à 360 px.
  - Quantité `×10` `font.small` `text.secondary` alignée à droite (x+w-16).
  - Badges entre le nom et la quantité.
- **Vide** : « Empty » en `disabled.ink`, fond `surface.panel` bordé 2 px `line.soft` (`SDL_RenderDrawRect` deux fois).
- **Focus** : cellule et pastille `select.bg`, tous les textes `select.ink`.
- **Modifiée** : pastille de numéro en `modified`, numéro en `ink.strong` (signal fort et lisible même sur une cellule focalisée).
- **Non empilable** : badge `1/slot`. Si la save contient une quantité > 1 (UX §5.7), icône `warn` juste avant la quantité, laissée telle quelle.
- **Détail du slot** (UX §2.3, « volet détail ») : dans la ligne de statut : « Slot 4 · Nook Miles Ticket · 0x16DB · ×10 · was Empty ».

### 6.6 Badges, puce de modifs, item en main
| Élément | Mesures | Recette |
|---|---|---|
| Favori ★ | icône 20×20 | `SDL_RenderCopy` de `star`. Repli : pastille `FAV` |
| `1/slot` (non empilable) | h 24, padding horizontal 8, chanfrein 2 | `fill(badge.single.bg)` + `font.micro` `badge.single.ink` |
| `WRITES NOW` (écriture NAND directe) | h 24, padding 8 | `fill(error.bg)` + `font.micro` `error.ink` |
| **Puce « N changes »** (en-tête) | h 40, en `(…, 36)`, padding 16 | `fill(modified)` + carré 8×8 `ink.strong` + « 4 changes » `font.small` `ink.strong` |
| **Puce « Up to date »** | h 40, padding 12 | `fill(ok)` + icône `check` + `font.small` `select.ink` |
| **Holding** (item en main) | texte seul | `font.small` `select.ink`, aligné à droite à 24 px de la puce ; « Holding: — » en `wood.ink` |
| Point d'onglet | 8×8 | `fill(modified)` |

Sur une ligne focalisée, les badges gardent leurs couleurs propres : ils restent lisibles.

### 6.7 Menu d'actions
- Ouvert par **A** sur l'élément focalisé (UX §3.1). **La 1re entrée est l'action par défaut**, la même que Y : elle porte une keycap `Y` à droite et le focus initial.
- Ancrage : à droite de l'élément quand il est dans la moitié gauche de l'écran, à gauche sinon. Bord haut aligné sur l'élément, puis recalé pour rester dans `{56,152,…,592}`.
- Mesures : largeur 480 ; en-tête 40 (`font.small` `text.secondary` : « Slot 4 · Nook Miles Ticket ×10 ») suivi d'un séparateur 2 px ; entrées de 36 px au pas de 40 ; padding 8 ; **9 entrées maximum** (40 + 9×40 + 12 = 412 px).
- Recette : voile `veil.light` sur `{0,0,1280,640}`, **ombre pleine** `fill(wood.dark, {x+6, y+6, w, h})`, cadre `fill(wood, {x, y, w, h})`, intérieur `fill(surface.panel, {x+3, y+3, w-6, h-6})`.
- Entrées : normale `text.primary` ; focus `select.bg` + `select.ink` ; grisée `disabled.ink` + raison (« Quantity: 1 (doesn't stack) ») ; destructive en mémoire (« Clear slot ») en `error` ; détail aligné à droite en `font.small` (« ×10 », « was Empty », ★).
- Pied d'aide pendant le menu : A Choose · Y grisé · B Close · L R, + et − grisés.

### 6.8 Dialogues
**Dialogue standard** (UX §1.3, §4.5, §4.6) :
- Voile `veil` sur `{0,0,1280,640}` via `SDL_SetRenderDrawBlendMode(BLEND)`. Le pied d'aide reste au-dessus et affiche les touches du dialogue.
- Boîte centrée, largeur 640 à 720, ombre pleine 6 px `wood.dark`, cadre `wood` 3 px, intérieur `surface.panel`.
- Bandeau titre de 56 px en `wood`, titre `font.title` `select.ink` en x+24. Le titre est une question (« Quit with 4 changes not applied? »).
- Corps : x+24, `font.body` `text.primary`, puis puces `•` en `font.small` `text.secondary`.
- **Boutons** en bas (y = bas − 80), hauteur 48, largeur = texte + 48, espacés de 16, chanfrein 4 :

| Style | Fond | Texte |
|---|---|---|
| Neutre | `surface.sunken` | `text.primary` |
| Focus | `select.bg` | `select.ink` |
| Destructif | `surface.panel` + cadre 2 px `error` | `error` |
| Destructif + focus | `error` | `select.ink` |

- Le **focus par défaut est toujours le choix sûr** (« Stay », « No », « Cancel »). ◀▶ déplacent le focus, A valide, B = choix sûr.

**Dialogue danger** (UX §3.4) :
- Même structure, mais cadre et bandeau titre en `error`, titre « Direct NAND write », sous-titre `font.title` (« Restore the SD backup? »).
- Boutons : `Cancel` (neutre, focus par défaut) et **`Hold A to restore`** (destructif + focus quand il est sélectionné).
- **Jauge** : barre de 8 px, 6 px sous le bouton, de la largeur du bouton : piste `error.bg`, remplissage `error` proportionnel au temps d'appui (1,5 s). Elle se vide si A est relâché.

### 6.9 Toast et ligne de statut
- Zone `{32,600,1216,40}`. Au repos, le détail du focus (`font.small` `text.secondary`).
- Toast : pastille `{48,604,auto,32}`, chanfrein 4, padding 12, `font.small`, affichée **3 s** (UX §1.3) :

| Type | Fond | Texte | Exemple |
|---|---|---|---|
| Succès | `ok` | `select.ink` | « Changes applied · SD backup updated with the previous state » |
| Info | `wood` | `select.ink` | « Holding: Nook Miles Ticket ×10 · Y to place » |
| Avertissement | `warn` | `ink.strong` | « Quantity set to 1 (doesn't stack) » |
| Erreur | `error` | `select.ink` | « SD backup failed · nothing was written · your changes are still pending » |

- Les erreurs bloquantes de UX §7 (écriture NAND, backup) passent par un **dialogue**, pas par un toast.

### 6.10 Keycaps et pied d'aide
- **Faces A/B/X/Y** : 32×32, chanfrein 8 (aspect rond). Fond `surface.panel`, lettre `font.small` `wood.dark` centrée.
- **L, R, +, −** : pilule 40×32, chanfrein 4, mêmes couleurs.
- **Grisée** (« off ») : fond `wood.dark`, lettre `wood.ink`, libellé `wood.mute`.
- Libellé à 8 px à droite de la keycap, `font.small` `select.ink`.
- **Pied d'aide à positions fixes** (UX §2.1) :
  - à gauche, depuis x=56 : `A` (libellé selon le focus) · `Y` (action par défaut, ou « — » grisé) · `B` (« Back », grisé hors modale) ;
  - à droite, aligné sur x=1232 : `L` `R` « Panel » · `+` « Review (4) » · `−` « Discard » ;
  - 28 px entre deux groupes.
- **+ mis en avant** quand il y a des modifs : pastille `modified` derrière la keycap et le libellé (`{x-8, 648, w+16, 40}`), libellé en `ink.strong`.
- X, ZL et ZR n'ont aucune action (UX §3.1) et n'apparaissent pas.

### 6.11 Écran Review (Récap)
- Ouvert par **+** depuis n'importe quel panneau (UX §4.3). Modal plein écran : la barre d'onglets devient un bandeau-titre « Review · 4 changes · nothing is written until you apply ».
- Liste groupée par panneau : en-têtes de groupe (6.3) « POCKETS · 3 », « BELLS & MILES · 1 ».
- Ligne de changement (pas de 44, onglet ambré) : cible `font.body` en x=80 (« Slot 4 », « Wallet ») ; avant en x=320 `text.secondary` ; `»` en x=700 ; après en x=740 `modified.ink`.
- Sous la liste : deux lignes `font.small` `text.primary` expliquant l'écriture et le backup.
- **Rangée de boutons** (y=520) : `Apply` (focus) · `Apply & quit` · `Discard all` (destructif) · `Quit without applying` · `Back`. Le Review **est** la confirmation d'Apply : pas de dialogue supplémentaire.
- ↑/↓ parcourent les changements puis les boutons. A sur un changement ouvre un menu d'actions : « Revert this change » / « Go to item ».
- Sans modif : « No pending changes » et seulement `Quit` (focus) et `Back`.
- Pendant l'écriture : **écran d'attente** (6.13).

### 6.12 Ligne Recherche (Catalog)
1re ligne de la liste, focalisable : `{56,192,1164,40}` en `surface.sunken`, cadre 2 px `wood` (3 px `select.bg` quand elle a le focus). « Search » `font.small` `text.secondary` en x=80, requête `font.body` `text.primary` en x=160, « A or Y to type » à droite. Le nombre de résultats est dans le bandeau de contexte (« 50 results for “golden” »).

### 6.13 États globaux
- **Écran d'attente** (« Writing… ») : dialogue standard sans bouton, titre « Writing… », corps « Don't turn off the console. ». Il est rendu une frame avant l'appel bloquant à `doSave()` (UX §4.4).
- **Vide** (aucun résultat, aucun favori) : texte centré `font.body` `text.secondary` + aide `font.small`, sans illustration.
- **Mode dégradé** (UX §7) : save non montable → écran bloquant (dialogue standard sans voile, sur fond de panneau vide) ; `items.txt` absent → bandeau `warn.bg` persistant sous la barre d'onglets, noms remplacés par « ID 16A2 ».

### 6.14 Stepper
- Modale d'édition d'un nombre (UX §1.3, §5.3), ancrée sous la ligne éditée. Voile `veil.light`.
- Boîte 528×256 (même recette que les dialogues, bandeau titre = nom de la valeur).
- Champ `{x+24, y+76, w-48, 64}` en `surface.sunken` : `‹` et `›` en `font.title` `text.secondary` aux extrémités, valeur `font.title` centrée (`modified.ink` si elle diffère de l'original).
- Dessous, en `font.small` `text.secondary` : « was 48,210 · 0 – 9,999,999 », puis le rappel des pas (« Left/right ±1,000 · up/down ±100,000 », « Hold 2 s: steps ×10 »).
- À la borne, la flèche concernée passe en `disabled.ink`.
- Pied d'aide : A Confirm · B Cancel (rétablit la valeur).
- Quantité d'un slot : même composant, bornes 1…99, pas ±1 / ±10.

### 6.15 Favoris : groupes « Yours » et « Built-in »
Décision du 2026-11-11 : le badge « SD » qui marquait un favori ajouté par l'utilisateur est **supprimé**. Il parlait du stockage (la carte SD), pas de ce que voit le joueur.
- La liste Favorites est coupée en deux **groupes**, chacun trié A→Z : d'abord **« YOURS · N »** (favoris ajoutés par l'utilisateur), puis **« BUILT-IN · N »** (favoris fournis avec l'app). Un groupe vide n'affiche pas son en-tête.
- En-tête de groupe : composant 6.3 (32 px, `surface.alt`, `font.small` MAJUSCULES `text.secondary`). Il n'est pas focalisable : ↑/↓ le sautent.
- Pagination : 8 lignes au pas de 44, un en-tête compte pour une ligne. Quand une page commence au milieu d'un groupe, on répète son en-tête avec « (cont.) ».
- Le menu d'actions garde la distinction : « Remove from favorites » pour un favori à vous, « Hide this built-in favorite » pour un favori fourni.

## 7. Écrans

| # | Maquette | Panneau / état (UX) | Points de charte illustrés |
|---|---|---|---|
| 01 | `01-pockets.html` | Poches, ouverture (§2.3) | 6.5, puce de modifs, Holding, point d'onglet, L grisé, + mis en avant |
| 02 | `02-pockets-actions.html` | Menu d'actions d'un slot (§5.1-5.5) | 6.7, pied d'aide grisé |
| 03 | `03-favorites.html` | Favoris (§2.3, §5.6) | groupes YOURS / BUILT-IN (6.15), 1/slot, pagination |
| 04 | `04-catalog.html` | Catalogue (§2.3, §5.2) | 6.12, ★, 1/slot, pagination |
| 05 | `05-bells-miles.html` | Argent & Miles + stepper (§5.3) | 6.4, 6.14, avertissement Wallet |
| 06 | `06-loadouts.html` | Loadouts (§5.8) | ligne double, aperçu |
| 07 | `07-system.html` | Système (§2.3, §5.9-5.10) | bandeau warn, danger bloqué, Info |
| 08 | `08-review.html` | Récap (§4.3) | 6.11 |
| 09 | `09-quit-dialog.html` | Quitter avec modifs (§4.6) | 6.8 dialogue standard, focus sur le choix sûr |
| 10 | `10-danger-dialog.html` | Restauration (§3.4, §5.9) | 6.8 dialogue danger, jauge, puce « Up to date » |
| 11 | `11-components.html` | Planche | tous les états |

## 8. Contraintes d'implémentation SDL2

- **Primitives autorisées** : `SDL_RenderFillRect`, `SDL_RenderDrawRect`, `SDL_RenderDrawLine`, `SDL_RenderCopy` (textures de texte et PNG), `SDL_SetRenderDrawBlendMode` pour les voiles. Aucune nouvelle bibliothèque : SDL2, SDL2_ttf et `stb_image.h` sont déjà là (`Makefile:25`, `source/stb_image.h`).
- **`SDL_RenderGeometry`** (SDL ≥ 2.0.18, fourni par les portlibs devkitPro actuelles) permettrait de vrais coins arrondis et des keycaps rondes en éventail de triangles. C'est optionnel : la recette chanfrein (6.1) reste la référence.
- **Texte** : `TTF_RenderUTF8_Blended` (déjà utilisé). Les textures sont **mises en cache** et reconstruites seulement quand le contenu change (`needDraw`, comme aujourd'hui), jamais à chaque frame. Centrage avec `SDL_QueryTexture`, troncature avec `TTF_SizeUTF8`. Seule la jauge du dialogue danger se redessine à chaque frame (rectangle, pas de texte).
- **Ordre de dessin** : fond → panneau → en-tête → onglets → contenu → statut → (voile → menu, stepper ou dialogue) → pied d'aide.
- **Motif de pois** : dessiné une fois dans une texture cible 1280×720 (`SDL_TEXTUREACCESS_TARGET`), sinon sauté.
- **Images** : `icon.png` (existant) et `ui_icons.png` (proposé), dans `sdmc:/switch/acnh_editor/`, avec un repli si le fichier est absent.
- **Exemple de tokens (documentation seulement, aucun fichier créé dans `source/`)** :

```cpp
// ui_theme.h — proposition
#pragma once
#include <SDL2/SDL.h>
namespace ui {
constexpr SDL_Color bgLeaf      {0x7A,0xC1,0x43,255};
constexpr SDL_Color panel       {0xF8,0xF5,0xEC,255};
constexpr SDL_Color panelAlt    {0xF1,0xEB,0xDD,255};
constexpr SDL_Color sunken      {0xE6,0xDC,0xC3,255};
constexpr SDL_Color lineSoft    {0xDD,0xD0,0xB4,255};
constexpr SDL_Color wood        {0x7A,0x5C,0x3E,255};
constexpr SDL_Color woodDark    {0x5C,0x44,0x30,255};
constexpr SDL_Color woodInk     {0xEA,0xDF,0xC8,255};
constexpr SDL_Color woodMute    {0xA8,0x8C,0x6C,255};
constexpr SDL_Color textPrimary {0x5A,0x40,0x29,255};
constexpr SDL_Color textSecond  {0x7A,0x5C,0x3E,255};
constexpr SDL_Color inkStrong   {0x3B,0x2A,0x1A,255};
constexpr SDL_Color selectBg    {0x3F,0x7A,0x2E,255};
constexpr SDL_Color selectInk   {0xF8,0xF5,0xEC,255};
constexpr SDL_Color tabIdle     {0x8B,0x6B,0x47,255};
constexpr SDL_Color modified    {0xE8,0xA3,0x3D,255};
constexpr SDL_Color modifiedInk {0xA6,0x5F,0x00,255};
constexpr SDL_Color warn        {0xF2,0xC1,0x4E,255};
constexpr SDL_Color warnBg      {0xFC,0xEF,0xC7,255};
constexpr SDL_Color warnInk     {0x7A,0x4B,0x00,255};
constexpr SDL_Color error       {0xC0,0x39,0x2B,255};
constexpr SDL_Color errorBg     {0xF9,0xE1,0xDD,255};
constexpr SDL_Color errorInk    {0xA3,0x2F,0x23,255};
constexpr SDL_Color ok          {0x2E,0x7D,0x32,255};
constexpr SDL_Color okBg        {0xE3,0xF1,0xE0,255};
constexpr SDL_Color okInk       {0x25,0x6B,0x29,255};
constexpr SDL_Color disabledInk {0xB8,0xAC,0x98,255};
constexpr SDL_Color favStar     {0xF2,0xB7,0x05,255};
constexpr SDL_Color singleBg    {0xFC,0xE3,0xC2,255};
constexpr SDL_Color singleInk   {0x8C,0x3A,0x10,255};
constexpr SDL_Color veil        {0x2A,0x1E,0x12,153};
constexpr SDL_Color veilLight   {0x2A,0x1E,0x12, 80};

constexpr SDL_Rect rPanel   {32, 24,1216,672};
constexpr SDL_Rect rHeader  {32, 24,1216, 64};
constexpr SDL_Rect rTabs    {32, 88,1216, 56};
constexpr SDL_Rect rContent {32,144,1216,456};
constexpr SDL_Rect rStatus  {32,600,1216, 40};
constexpr SDL_Rect rHelp    {32,640,1216, 56};
constexpr int LIST_X = 56, LIST_Y = 192, ROW_H = 40, ROW_PITCH = 44, ROWS_PER_PAGE = 8;
constexpr int GRID_Y = 188, CELL_H = 36, CELL_PITCH = 40, CELL_W = 576;
constexpr int DROW_H = 64, DROW_PITCH = 68;
constexpr int FONT_DISPLAY = 32, FONT_TITLE = 28, FONT_BODY = 24, FONT_SMALL = 20, FONT_MICRO = 16;
constexpr Uint32 TOAST_MS = 3000, HOLD_CONFIRM_MS = 1500;

// Coin « chanfrein » : deux rectangles
inline void fillRound(SDL_Renderer* r, SDL_Rect a, int k, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect h{a.x + k, a.y, a.w - 2*k, a.h}, v{a.x, a.y + k, a.w, a.h - 2*k};
    SDL_RenderFillRect(r, &h); SDL_RenderFillRect(r, &v);
}
}
```

## 9. Libellés FR ↔ EN et ton

L'interface est **en anglais** (choix de l'utilisateur ; public international du Homebrew App Store, comme l'app actuelle). Cela répond à la question UX Q1 pour la v1. Correspondance avec les termes de la spec UX :

| Spec UX (FR) | Interface (EN) |
|---|---|
| Poches / Favoris / Catalogue | Pockets / Favorites / Catalog |
| Argent & Miles (Wallet, Bank, Miles, Loan) | Bells & Miles (Wallet, Bank, Nook Miles, House loan) |
| Loadouts / Système | Loadouts / System |
| Puce « ● N modifs » / « ✓ À jour » | « ■ 4 changes » / « ✓ Up to date » (carré et icône dessinés) |
| Item en main | Holding: … |
| Poser+suivant / Poser ici / Prendre en main | Place + next / Place here / Take in hand |
| Prendre (Favoris, Catalogue) | Take |
| Quantité… / Quantité en main… | Quantity… / Held quantity… |
| Remplir les slots vides | Fill empty slots |
| Vider le slot / Rétablir l'original | Clear slot / Revert to original |
| Saisir un ID (hex)… | Enter item ID (hex)… |
| Ajouter aux / Retirer des favoris | Add to favorites / Remove from favorites |
| Récap | Review |
| Appliquer / Appliquer et quitter | Apply / Apply & quit |
| Tout annuler (−) | Discard all · pied d'aide : « Discard » |
| Quitter sans appliquer / Rester | Quit without applying / Stay |
| Dialogue danger « Écriture directe en NAND » | Direct NAND write |
| Maintenir Ⓐ pour confirmer | Hold A to restore / Hold A to repair |
| Restaurer le backup SD → NAND | Restore SD backup to console |
| Écrit directement (badge) | WRITES NOW |
| Non empilable | `1/slot` (badge), « doesn't stack » (texte) |
| Charger / Enregistrer (loadout) | Load / Save current pockets here |

Ton : phrases courtes, sans point d'exclamation, pas de MAJUSCULES criardes comme « SAVED » ou « FAILED » aujourd'hui. Les messages disent ce qui s'est passé et quoi faire : « SD backup failed · nothing was written · your changes are still pending ». Les titres de dialogue sont des questions (« Quit with 4 changes not applied? »), les boutons des verbes (« Apply & quit »).

## 10. Questions ouvertes

1. **Couleur de sélection** : vert foncé `#3F7A2E` + texte crème (proposé, 4.77) ou vert actuel `#6AA84F` + texte brun foncé (4.78, plus clair et joyeux, mais le texte sélectionné change d'aspect) ?
2. **Motif de pois** sur le fond vert : on le garde (plus « Animal Crossing », coût nul une fois mis en texture) ou fond uni comme aujourd'hui ?
3. **Planche `ui_icons.png`** (6 icônes, un fichier de plus sur la SD) ou tout en dessin pur avec les textes de repli (★ → `FAV`, ✓ → texte seul) ?
4. **`SDL_RenderGeometry`** pour de vrais arrondis et des keycaps rondes, ou chanfrein seul, plus simple à maintenir ?
5. **Grille des poches** : 2 colonnes × 10 (lisible, ordre actuel 1-10 / 11-20, retenu par la spec) ou 10 × 2 comme dans le jeu (fidèle, mais noms tronqués à ~10 caractères) ?
6. **5 tailles de police** : OK, ou on supprime `micro 16` (badges en 20 px, plus larges) ?
7. **Détail du slot** dans la ligne de statut (proposé, pas de place perdue) ou volet dédié, ce qui réduirait la grille ?
8. **Version affichée** : seulement dans System › Info (proposé) ou aussi dans l'en-tête ?
9. **Avertissement Wallet > 99,999** (UX Q4) : la maquette montre la proposition par défaut de la spec (sous-ligne ambrée + icône). À confirmer.
