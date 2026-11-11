# ACNH Power Pockets — Glyphes d'items

> Statut : **v2** (2026-11-11), après les choix de l'utilisateur sur la v1. Complète [`CHARTE.md`](CHARTE.md) (§4 icônes, §6.3 lignes de liste, §6.5 grille des poches).
>
> Livrables : [`tools/make_item_glyphs.py`](../../tools/make_item_glyphs.py) (dessin, mesure de couverture, aperçu), [`assets/item_glyphs.png`](../../assets/item_glyphs.png) (planche), [`assets/item_glyphs.txt`](../../assets/item_glyphs.txt) (table des glyphes, couleurs, règles), [`mockups/12-glyphs.html`](mockups/12-glyphs.html) (contrôle ×4 sur les deux fonds, teintes, grille des poches et Catalog avec glyphes).
>
> L'intégration dans `source/` reste à faire (§6).

## Sommaire
1. [En bref](#1-en-bref)
2. [Décisions prises](#2-décisions-prises)
3. [Les 98 glyphes](#3-les-98-glyphes)
4. [Principes de dessin](#4-principes-de-dessin)
5. [Teinte selon la couleur de l'objet](#5-teinte-selon-la-couleur-de-lobjet)
6. [Intégration dans l'UI (SDL2)](#6-intégration-dans-lui-sdl2)
7. [Règles de correspondance item → glyphe](#7-règles-de-correspondance-item--glyphe)
8. [Couverture mesurée](#8-couverture-mesurée)
9. [Droit d'auteur : ce qui a été évité et pourquoi](#9-droit-dauteur--ce-qui-a-été-évité-et-pourquoi)
10. [Ajouter ou modifier un glyphe](#10-ajouter-ou-modifier-un-glyphe)
11. [Questions ouvertes](#11-questions-ouvertes)

---

## 1. En bref

- **98 glyphes** : 97 catégories et un glyphe `unknown` (« ? »). Un glyphe par **catégorie**, pas par objet : `items.txt` compte 10 885 noms sans catégorie.
- **32 glyphes teintables** : fleurs, buissons, sachets de graines, citrouille, lit, canapé, tapis, vêtements, accessoires, sacs et parapluie. Ils prennent la couleur écrite dans le nom de l'objet (« straw hat (Red) », « white-tulip stems »).
- **Planche** : PNG RGBA de 2352×72 px, une colonne de 24 px par glyphe, sur 3 rangées (§5). Elle se charge avec `stb_image` comme `ui_icons.png`.
- **Couverture** :
  - **94,0 %** des objets nommés sont reconnus par une règle ;
  - les **6,0 %** restants affichent « ? » ;
  - **49,1 %** de tous les objets s'affichent dans leur couleur (86,9 % des objets qui tombent sur un glyphe teintable).
- **Lisibilité** : contour brun foncé et fin liseré crème autour de chaque glyphe. Chacun a été contrôlé à ×4, à 24 et à 20 px, sur le crème `#F1EBDD` et sur la sélection `#3F7A2E`, avec des teintes claires (blanc, jaune) et foncées (noir, bleu marine).

## 2. Décisions prises

| Question (v1) | Décision |
|---|---|
| Image des objets non reconnus | **« ? »** (`unknown *`), plus le fauteuil par défaut |
| Noms japonais internes du jeu | **« ? »**, forcé par ID quand une règle les attrapait |
| Taille dans les poches | **24 px**, glyphe en x+52, nom en x+84 |
| Plus de variété | **Oui** : fruits et légumes dédiés, types de fleurs, arbres et végétation, mobilier détaillé (11 glyphes), vêtements détaillés (17 glyphes) |
| Couleur de l'objet | **Oui** : glyphes teintables (§5) |
| Source des catégories | **Nos règles** : pas de base externe |
| Lecture des règles | **Au lancement**, depuis la carte SD (éditables) |
| Navet | **Un dessin par fruit et par légume** : le navet ne sert plus qu'aux navets |

## 3. Les 98 glyphes

« Teinte » = nombre d'objets dont la couleur est reconnue dans le nom.

| # | Nom | Teintable | Objet dessiné | Exemples | Objets | Teinte |
|---|---|---|---|---|---|---|
| 0 | `unknown` |  | disque sable avec « ? » | non reconnu, noms japonais internes | 655 |  |
| 1 | `axe` |  | hache | flimsy axe, golden axe | 6 |  |
| 2 | `net` |  | épuisette | net, golden net | 8 |  |
| 3 | `fishing_rod` |  | canne, fil, flotteur | fishing rod | 7 |  |
| 4 | `shovel` |  | pelle | shovel | 7 |  |
| 5 | `watering_can` |  | arrosoir | watering can | 6 |  |
| 6 | `slingshot` |  | lance-pierre | slingshot | 4 |  |
| 7 | `ladder` |  | échelle | ladder | 6 |  |
| 8 | `vaulting_pole` |  | perche rayée | vaulting pole | 1 |  |
| 9 | `wand` |  | baguette, étincelle à 4 branches | star wand, cosmos wand | 25 |  |
| 10 | `apple` |  | pomme | apple | 1 |  |
| 11 | `orange` |  | orange | orange | 1 |  |
| 12 | `pear` |  | poire | pear | 1 |  |
| 13 | `peach` |  | pêche | peach | 1 |  |
| 14 | `cherry` |  | deux cerises | cherry | 1 |  |
| 15 | `coconut` |  | noix de coco | coconut | 1 |  |
| 16 | `turnip` |  | navet | turnips | 12 |  |
| 17 | `tomato` |  | tomate | tomato, ripe tomato plant | 2 |  |
| 18 | `wheat` |  | épi de blé | wheat | 2 |  |
| 19 | `sugarcane` |  | cannes à sucre | sugarcane | 2 |  |
| 20 | `potato` |  | pomme de terre | potato | 3 |  |
| 21 | `carrot` |  | carotte | carrot | 2 |  |
| 22 | `pumpkin` | oui | citrouille | yellow pumpkin | 8 | 8 |
| 23 | `bamboo_shoot` |  | pousse de bambou | bamboo shoot | 1 |  |
| 24 | `rose` | oui | rose | red roses, rose stems | 36 | 36 |
| 25 | `tulip` | oui | tulipe | white tulips | 28 | 28 |
| 26 | `pansy` | oui | pensée | purple pansies | 24 | 24 |
| 27 | `cosmos` | oui | cosmos à 8 pétales | orange cosmos | 24 | 24 |
| 28 | `lily` | oui | lys étoilé | white lilies | 25 | 24 |
| 29 | `hyacinth` | oui | grappe de jacinthe | blue hyacinths | 28 | 28 |
| 30 | `windflower` | oui | anémone à cœur sombre | red windflowers | 24 | 24 |
| 31 | `mum` | oui | chrysanthème pompon | yellow mums | 24 | 24 |
| 32 | `flower` | oui | fleur à 5 pétales | carnations, lily-of-the-valley | 6 | 0 |
| 33 | `tree` |  | arbre rond | hardwood tree, apple tree | 23 |  |
| 34 | `cedar` |  | conifère | cedar tree | 1 |  |
| 35 | `palm` |  | palmier | coconut tree | 1 |  |
| 36 | `bamboo` |  | tiges de bambou | bamboo tree | 1 |  |
| 37 | `bush` | oui | buisson fleuri | pink-azalea bush | 24 | 22 |
| 38 | `weeds` |  | touffe d'herbe | clump of weeds | 3 |  |
| 39 | `sapling` |  | pousse sur motte | sapling, sprouts | 108 |  |
| 40 | `seed_bag` | oui | sachet, fleur imprimée | red-rose bag | 46 | 36 |
| 41 | `mushroom` |  | champignon brun | flat mush | 7 |  |
| 42 | `wood` |  | bûche | wood, softwood | 8 |  |
| 43 | `ore` |  | pierre à éclat doré | stone, iron nugget | 6 |  |
| 44 | `star_shard` |  | cristal | star fragment | 15 |  |
| 45 | `seasonal` |  | flocon | ornaments, acorn | 23 |  |
| 46 | `shell` |  | coquille | cowrie, pearl | 12 |  |
| 47 | `fish` |  | poisson | sea bass, koi | 80 |  |
| 48 | `insect` |  | scarabée | stag beetles | 79 |  |
| 49 | `sea_creature` |  | crabe | octopus, sea star | 37 |  |
| 50 | `fossil` |  | os | T. rex skull | 72 |  |
| 51 | `figurine` |  | buste sur socle | X model, gyroids | 256 |  |
| 52 | `painting` |  | tableau | famous painting | 46 |  |
| 53 | `photo` |  | tirage, silhouette neutre | X's photo | 481 |  |
| 54 | `poster` |  | affiche punaisée | X's poster | 495 |  |
| 55 | `furniture` |  | cube de bois (décor divers) | trophies, toys, mirrors | 282 |  |
| 56 | `chair` |  | chaise | chairs, stools, benches | 113 |  |
| 57 | `table` |  | table | tables, desks, counters | 129 |  |
| 58 | `bed` | oui | lit, couverture | beds | 45 | 0 |
| 59 | `sofa` | oui | canapé | sofas | 19 | 0 |
| 60 | `storage` |  | commode | shelves, cabinets, boxes | 140 |  |
| 61 | `lamp` |  | lampe | lamps, lanterns | 82 |  |
| 62 | `appliance` |  | lave-linge | TV, fridge, toilet, stove | 129 |  |
| 63 | `instrument` |  | guitare | piano, drum set | 17 |  |
| 64 | `houseplant` |  | plante en pot | plants, planters, vases | 20 |  |
| 65 | `clock` |  | pendule | clocks | 27 |  |
| 66 | `outdoor` |  | panneau en bois | mailboxes, signs, fountains | 174 |  |
| 67 | `rug` | oui | tapis ovale | rugs, mats | 196 | 113 |
| 68 | `wallpaper` |  | rouleau de papier peint | X wall | 310 |  |
| 69 | `flooring` |  | dalle de parquet | X flooring | 193 |  |
| 70 | `fence` |  | clôture | X fence | 33 |  |
| 71 | `shirt` | oui | tee-shirt | shirts, tees, sweaters | 891 | 749 |
| 72 | `coat` | oui | manteau | jackets, coats, parkas | 370 | 350 |
| 73 | `dress` | oui | robe | dresses, kimonos | 552 | 513 |
| 74 | `outfit` | oui | combinaison ceinturée | uniforms, costumes, suits | 609 | 540 |
| 75 | `pants` | oui | pantalon | pants, shorts | 524 | 509 |
| 76 | `skirt` | oui | jupe | skirts | 295 | 282 |
| 77 | `hat` | oui | chapeau | hats, helmets, hoods | 448 | 402 |
| 78 | `cap` | oui | casquette | caps | 236 | 220 |
| 79 | `hair` | oui | nœud | wigs, hairpins, crowns | 197 | 119 |
| 80 | `glasses` | oui | lunettes | glasses, goggles | 182 | 170 |
| 81 | `mask` | oui | masque | masks, noses | 123 | 99 |
| 82 | `socks` | oui | chaussette | socks, tights | 333 | 315 |
| 83 | `shoes` | oui | basket | shoes, sandals | 406 | 372 |
| 84 | `boots` | oui | botte | boots | 99 | 90 |
| 85 | `bag` | oui | sac à main | bags, purses | 157 | 123 |
| 86 | `backpack` | oui | sac à dos | backpacks | 96 | 84 |
| 87 | `umbrella` | oui | parapluie | umbrellas | 73 | 17 |
| 88 | `recipe` |  | feuille et crayon | DIY recipes | 20 |  |
| 89 | `music` |  | deux croches | K.K. songs | 109 |  |
| 90 | `ticket` |  | billet perforé | Nook Miles Ticket | 5 |  |
| 91 | `bells` |  | pile de pièces | 99,000 Bells | 114 |  |
| 92 | `present` |  | paquet cadeau | present, wrapping paper | 37 |  |
| 93 | `food` |  | bol fumant | pies, curry | 195 |  |
| 94 | `drink` |  | verre à paille | smoothies | 36 |  |
| 95 | `egg` |  | œuf décoré | Bunny Day eggs | 7 |  |
| 96 | `kit` |  | boîte à outils | customization kit | 27 |  |
| 97 | `document` |  | feuille cornée | permits, books | 29 |  |

## 4. Principes de dessin

- **Formes simples et rondes**, lisibles à 24 px : une silhouette, deux ou trois détails au plus, de face ou de profil.
- **Contour unique** `wood.dark #5C4430` d'environ 1,3 px. Les pétales fins et les nervures ont un contour réduit à 0,8 px, sinon le contour mange la couleur.
- **Halo crème** `#F8F5EC` d'environ 1 px autour de la silhouette. Il disparaît sur le crème des listes et détoure le glyphe sur le vert de sélection.
- **Palette** : tokens de la charte, plus quelques couleurs d'objets désaturées : bois clair `#C28E5C`, rouge `#D9534A`, bleu `#5FA8D3` / `#3D7EA6`, orange `#F08A2E`, rose `#F29BB0`, violet `#9B7BC8`, métal `#B4BEC6`, os `#EEE2C6`, pierre `#A8A298`.
- **Fabrication** : dessin à 16× (384×384 px), halo, puis réduction Lanczos à 24×24.
- **Distinctions voulues** :
  - `star_shard` est un cristal et `wand` porte une étincelle à 4 branches, pour ne pas se confondre avec le badge favori ★ ;
  - `vaulting_pole` (perche rayée) n'a pas de fil, contrairement à `fishing_rod` ;
  - `furniture` (cube de bois) ne ressemble à aucun meuble précis : c'est le décor divers.
- **Corrections après contrôle** (v1 et v2) : `crop`/`turnip` (feuilles en barreaux), `wallpaper` (ressemblait à une poubelle), `seed_bag` (à un dé), `cosmos`, `lily` et `mum` (pétales mangés par le contour), `coconut` (trois points formaient un visage), `bamboo_shoot` (pyramide sombre), `palm` (palmes trop fines), `bush` (ressemblait à une mare).

## 5. Teinte selon la couleur de l'objet

**Planche à 3 rangées** (une colonne de 24 px par glyphe, x = index × 24) :

| Rangée | y | Contenu | Usage |
|---|---|---|---|
| 0 | 0 | glyphe complet, couleurs par défaut | objet sans couleur reconnue, ou glyphe non teintable |
| 1 | 24 | **masque** des parties à teinter : blanc (clair) et gris 200 (ombre) | dessiné avec `SDL_SetTextureColorMod(r, g, b)` |
| 2 | 48 | contours, détails non teintés et halo, sans la zone teintée | dessiné par-dessus la rangée 1 |

Les rangées 1 et 2 sont vides pour les glyphes non teintables. La multiplication de SDL donne la couleur exacte sur le blanc et une version plus sombre (× 200/255) sur le gris, d'où le relief. Dans le script, les parties teintables sont dessinées avec deux couleurs marqueurs (magenta `FF00FF` et `C800C8`), puis séparées avant la réduction.

**Couleur d'un objet** :
1. On lit d'abord la **variante entre parenthèses** : `straw hat (Red)` → red, `cycling shorts (Purple & pink)` → purple (première couleur citée ; à chaque position, le nom de couleur le plus long gagne, donc `light blue` avant `blue`).
2. Sans variante colorée, on lit le **nom de base** : `white-tulip stems` → white, `pink-azalea bush` → pink, `black chic umbrella` → black.
3. Les couleurs ne sont reconnues qu'en **mots entiers** : `golden axe` ne donne pas `gold`.
4. Sans couleur reconnue (`(No Variations)`, `(Forest print)`, `(Love)`…), on affiche la rangée 0.

**Vocabulaire** : 86 couleurs (`color <RRGGBB> <nom>` dans `item_glyphs.txt`), relevées dans les variantes de `items.txt` (white, black, navy blue, berry red, avocado, mustard, peacock blue, wisteria, rose gold, baby pink…). Valeurs choisies un peu adoucies pour rester dans la palette. Les plus utilisées : blue 616, red 557, green 550, black 461, pink 449, white 368, yellow 354, purple 352, orange 258, brown 205. Toutes servent au moins une fois.

**Lisibilité des teintes** : le contour brun et le liseré crème sont dans la rangée 2, jamais teintés. Un vêtement blanc reste détouré sur le crème, un vêtement noir ou bleu marine reste détouré sur le vert (vérifié dans `12-glyphs.html`).

## 6. Intégration dans l'UI (SDL2)

| Endroit | Glyphe | Texte |
|---|---|---|
| **Cellule de poche** (`drawPockets`, 576×36) | `{r.x+52, r.y+6, 24, 24}` | nom en `r.x+84` (au lieu de +56), largeur max réduite de 28 px (≈ 332 px) |
| **Ligne de liste** Favorites / Catalog (`itemRow`, 1164×40) | `{r.x+52, r.y+8, 24, 24}` ; ★ du Catalog en `r.x+24` | nom en `r.x+84` |
| **Review** | devant « avant » (x=320) et « après » (x=740) | texte décalé de 32 px |
| **En-tête « Holding: »** | 20 px devant le nom | — |

Dessin d'un glyphe (texture chargée une fois) :

```cpp
// id -> glyph index (uint8_t, computed at launch) and color (0 = none)
void drawGlyph(SDL_Renderer* ren, SDL_Texture* sheet, int glyph, bool tint, uint32_t rgb,
               int x, int y, int size /* 24 or 20 */) {
    SDL_Rect dst = { x, y, size, size };
    if (tint && rgb) {
        SDL_Rect mask = { glyph * 24, 24, 24, 24 }, over = { glyph * 24, 48, 24, 24 };
        SDL_SetTextureColorMod(sheet, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        SDL_RenderCopy(ren, sheet, &mask, &dst);
        SDL_SetTextureColorMod(sheet, 255, 255, 255);
        SDL_RenderCopy(ren, sheet, &over, &dst);
    } else {
        SDL_Rect full = { glyph * 24, 0, 24, 24 };
        SDL_RenderCopy(ren, sheet, &full, &dst);
    }
}
```

- **Calcul au lancement** (choix retenu) : deux tableaux par ID, `uint8_t glyphOf[]` et `uint32_t colorOf[]` (environ 75 Ko). Ils sont calculés après `items.txt` et recalculés par « Reload… » du panneau System.
  - Ordre de grandeur : 825 règles × 10 885 noms ≈ 8 980 milliers de comparaisons.
  - Précalculer une fois les noms en minuscules et les noms de base.
  - Comme `StackRules`, appliquer chaque règle à tous les objets ; la dernière qui correspond l'emporte.
- **Parseur** : `StackRules::applyRule` gère déjà le nom exact, `~` et `0xID`. Il faut y ajouter :
  - `$mot` (fin du nom de base) ;
  - les plages `0xA-0xB` ;
  - `*` ;
  - le préfixe `<glyphe>` ;
  - les lignes `glyph N nom [tint]` et `color RRGGBB nom`.
- **Replis** :
  - si `item_glyphs.png` est absent, aucun glyphe et mise en page actuelle (nom en x+56) ;
  - si `item_glyphs.txt` est absent, aucun glyphe non plus ;
  - un ID hors `items.txt` donne le glyphe 0 ;
  - un slot vide n'a pas de glyphe ;
  - un objet sans couleur utilise la rangée 0.

## 7. Règles de correspondance item → glyphe

Fichier `assets/item_glyphs.txt` (825 règles), à copier dans `sdmc:/switch/acnh_editor/`.

```
glyph 71 shirt tint      # table : index, nom, teintable
color 34497A navy blue   # couleur reconnue dans les noms
shirt $tee               # règle : <glyphe> <motif>
```

| Motif | Sens | Exemple |
|---|---|---|
| `golden axe` | nom exact (insensible à la casse) | `axe golden axe` |
| `~texte` | le nom contient le texte | `music ~K.K.` |
| `$mot` | le **nom de base** se termine par ce mot (après une espace, un tiret ou rien) | `dress $dress` |
| `0x16DB` | un ID | `unknown 0x0A87` |
| `0x08A7-0x08EC` | une plage d'IDs incluse | `fish 0x08A7-0x08EC` |
| `*` | tous les objets (première règle) | `unknown *` |

- **Nom de base** : le nom sans la variante « (…) » et sans la partie « with … » (`cook cap with logo (Blue)` → `cook cap`, `curry with rice` → `curry`).
- **Ordre** : de haut en bas, **la dernière règle qui correspond gagne**. Le fichier va du général au particulier :
  1. `unknown *` ;
  2. gyroïdes ;
  3. vêtements par dernier mot ;
  4. mobilier par dernier mot ;
  5. plantes, cuisine, matériaux ;
  6. musique, papiers, outils ;
  7. blocs d'IDs (poissons, insectes, créatures marines) ;
  8. corrections ;
  9. types de fleurs, arbres, mots de mobilier supplémentaires ;
  10. noms japonais forcés en « ? ».
- **Sources** : uniquement `items.txt` (noms, blocs d'IDs contigus observés dans ce fichier) et des noms communs anglais. Rien d'importé (en particulier rien de NHSE, GPLv3).
- **Fragilité** : les plages d'IDs suivent l'ordre de `items.txt`. Si ce fichier est réordonné, `--coverage` signale les règles devenues inutiles.

## 8. Couverture mesurée

Mesure reproductible :

```bash
python3 tools/make_item_glyphs.py --coverage
```

| Mesure | Valeur |
|---|---|
| Objets avec un nom | 10 885 |
| Reconnus par une règle | **10 230 (94,0 %)** |
| Affichés « ? » | 655 (6,0 %) |
| Sur un glyphe teintable | 6 148 (56,5 %) |
| Affichés dans leur couleur | **5 345 (49,1 % du total, 86,9 % des teintables)** |
| Règles / couleurs / glyphes | 825 / 86 / 98 (aucune règle morte, aucune ligne invalide) |
| Erreurs sur un échantillon aléatoire de 120 objets | 1 (`cute DIY table`), corrigée |

**Plus gros groupes affichés « ? »** (dernier mot du nom) : `again?` 4, `scabbard` 3, `delivery` 3, `sword` 3, `item` 3, `setting` 3, `vista` 3, `corner` 3, `topiary` 3, `stump` 3, `treat` 3, `cat` 2, `flamingo` 2, `grinder` 2, `skeleton` 2, `breaker` 2… Aucun groupe ne dépasse 4 objets. Ce sont surtout des objets de décor aux noms uniques, des objets tenus en main (`sword`, `scabbard`) et les placeholders du jeu.

## 9. Droit d'auteur : ce qui a été évité et pourquoi

**Méthode** : chaque glyphe est une fonction Python qui pose des formes géométriques (ellipses, polygones, traits). Aucune image n'a été lue, téléchargée, décalquée ou consultée comme modèle : ni capture du jeu, ni site de fans, ni sprite. Les objets sont génériques (une pomme, une chemise, un lit), dans le style de la charte.

| Signe distinctif du jeu | Remplacement retenu | Raison |
|---|---|---|
| Feuille « mordue » (logo Animal Crossing) | feuilles toujours **ovales et entières** ; aucune feuille seule comme glyphe | emblème de la marque |
| Sac de clochettes à étoile | **pile de pièces** (`bells`) | dessin caractéristique du jeu |
| Logo Nook, tanuki, personnages | aucun personnage ; silhouette neutre (`photo`, `poster`), **buste générique** (`figurine`) | personnages protégés |
| Gyroïdes | buste générique partagé avec modèles et statues | forme propre au jeu |
| Fossile non identifié (pierre à empreinte) | **os** | icône caractéristique |
| Carte de recette DIY | **feuille lignée et crayon** | évite la mise en page du jeu |
| Ticket Nook Miles | **billet perforé générique** | évite le visuel d'origine |
| Champignon rouge à pois blancs | **chapeau brun uni** | évoque une autre licence (objets crossover Mario présents dans `items.txt`) |
| Étoiles (fragments, baguette) | **cristal** et **étincelle à 4 branches** | lisibilité et éloignement des icônes du jeu |

- **Navet** : il n'apparaît plus que pour les 12 objets « turnips ». Le dessin reste générique (racine blanche, collet violet, feuilles ovales).
- Le « ? » de `unknown` est tracé avec `assets/font.ttf` (Varela Round, SIL OFL 1.1), qui autorise ce rendu en image.

## 10. Ajouter ou modifier un glyphe

1. Dans `tools/make_item_glyphs.py`, écrire `g_<nom>(g)` avec les aides `g.ell`, `g.circ`, `g.poly`, `g.rect`, `g.stick`, `g.line`, `g.arc`, `leaf`, `petal`, `sparkle`. Coordonnées 0 à 24, marge de 2 pour le halo.
2. Pour un glyphe teintable, peindre les parties à recolorer avec `T` (clair) et `TS` (ombre). Donner sa couleur par défaut dans `DEFAULT_TINT`.
3. Dans `assets/item_glyphs.txt`, ajouter `glyph <N> <nom>` (avec `tint` si besoin) **à la fin de la table** : les index restent contigus et ne changent jamais (`0 = unknown`). L'outil refuse une table incohérente, ou un drapeau `tint` qui ne correspond pas au dessin.
4. Ajouter les règles, et si besoin des `color`.
5. Générer et contrôler :

```bash
python3 tools/make_item_glyphs.py --preview /tmp/glyphs_x4.png
```

```bash
python3 tools/make_item_glyphs.py --coverage
```

6. Vérifier à ×4 et à 24 px dans `docs/design/mockups/12-glyphs.html`, via un serveur local. Cette page est générée : sa liste de cartes doit être régénérée après un ajout.

## 11. Questions ouvertes

1. **Objets tenus en main et décor unique** restés en « ? » (655) : ajouter un glyphe « objet divers » (par exemple une épée pour `sword`/`scabbard`), ou accepter le « ? » ?
2. **Teinte des murs et sols** (`blue diner wall`, `red dot flooring`) : les rendre teintables aussi (503 objets de plus) ?
3. **Objets bicolores** (`Black & blue`) : seule la première couleur est utilisée. Une seconde teinte (ombre) vaut-elle la complexité ?
4. **`(No Variations)`** : 803 objets teintables sans couleur dans le nom (surtout des vêtements) gardent la couleur par défaut du glyphe. Faut-il une couleur neutre (beige) plutôt que le bleu, le rose ou le vert par défaut ?
