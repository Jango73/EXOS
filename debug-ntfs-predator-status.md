# NTFS/NVMe Predator - Journal de debug et état de recherche

Date: 2026-02-12
Contexte: debug bare metal (Predator), problème d'accès NTFS sur NVMe via `/fs/hd0p2`.

## Objectif
- Corriger les échecs `dir`/`cd` sur volume NTFS NVMe.
- Réduire le flood de logs sans masquer les signaux utiles.
- Isoler strictement les logs de boot (`T0`) des logs runtime de commande shell (`Txxxxx` après boot).

## Symptômes observés (terrain)
- `dir /fs/hd0p2` retourne souvent: `Unable to read on volume hd0p2, reason : file system driver refused open/list`.
- Flood récurrent: `[NtfsLoadFileRecordBuffer] Invalid file record magic=...`.
- Dans les logs récents, les erreurs de commande `dir` sont en `T19xxx`/`T138xxx` (runtime), distinctes des `T0` du boot.

## Changements effectués

### 1) Infrastructure timeout early boot
Fichiers:
- `kernel/include/Clock.h`
- `kernel/source/Clock.c`

Changement:
- Ajout de `HasOperationTimedOut(StartTime, LoopCount, LoopLimit, TimeoutMilliseconds)`.
- Objectif: ne pas dépendre uniquement de `GetSystemTime` avant `EnableInterrupts`.

### 2) Documentation règles agent
Fichiers:
- `AGENTS.md`

Changements:
- Rappel explicite: `GetSystemTime` n'avance pas en early boot avant `EnableInterrupts`.
- Référence explicite à `KernelLogTagFilter`.
- Ajout section "Architecture and Reuse Rules (MANDATORY)" demandant modules réutilisables au lieu de hacks locaux.

### 3) Module générique de rate-limit
Fichiers:
- `kernel/include/utils/RateLimiter.h` (nouveau)
- `kernel/source/utils/RateLimiter.c` (nouveau)
- `documentation/Kernel.md`

Changement:
- Création d'un module générique `RateLimiter` (budget immédiat + cooldown + compteur supprimé).

### 4) Limitation du flood NTFS `Invalid file record magic`
Fichier:
- `kernel/source/drivers/NTFS-Record.c`

Changement:
- Le warning est limité (budget initial puis périodique), avec `suppressed=%u`.

### 5) Durcissement parsing/index NTFS
Fichiers:
- `kernel/source/drivers/NTFS-Base.c`
- `kernel/source/drivers/NTFS-Private.h`
- `kernel/source/drivers/NTFS-Record.c`
- `kernel/source/drivers/NTFS-Index.c`
- `kernel/source/drivers/NTFS-Path.c`

Changements:
- Ajout `NtfsIsValidFileRecordIndex()` (validation géométrique de l'index MFT).
- Validation d'index avant `NtfsLoadFileRecordBuffer`.
- Décodage file-reference NTFS renforcé.
- Durcissement de la traversée d'index (moins permissive sur métadonnées incohérentes).
- Instrumentation ciblée sur échecs de résolution path et énumération.
- Ajout de compteurs diag dans `NTFS_FOLDER_ENUM_CONTEXT` (`ref_invalid`, `idx_invalid`, `record_read_fail`, `seq_mismatch`).

### 6) Filtre de logs orienté debug courant
Fichier:
- `kernel/source/Log.c`

Changements:
- `KERNEL_LOG_TAG_FILTER_MAX_LENGTH` augmenté à 512.
- Filter enrichi avec tags NVMe/NTFS pertinents (`NtfsEnumerateFolderByIndex`, `NtfsReadFileRecord`, `NtfsResolvePathToIndex`, `NtfsLookupChildByName`, etc.).
- Ajout runtime `OpenFile`, `ResolvePath` pour tracer le chemin de la commande shell.

### 7) Instrumentation runtime SystemFS
Fichier:
- `kernel/source/SystemFS.c`

Changements:
- Logs explicites dans `OpenFile`:
  - contexte de résolution (`path`, `wildcard`, `node`, `mount_path`, `remaining`),
  - échecs d'ouverture déléguée (direct/wildcard/mounted).

### 8) Correctif NTFS VFS wildcard enumeration
Fichier:
- `kernel/source/drivers/NTFS-VFS.c`

Changement:
- `NtfsLoadCurrentEnumerationEntry()` ne casse plus sur la première entrée illisible.
- Il saute les entrées mortes et continue la recherche d'une entrée lisible.

### 9) Travaux NVMe déjà intégrés dans l'état courant
Fichiers:
- `kernel/include/drivers/NVMe-Core.h`
- `kernel/include/drivers/NVMe-Internal.h`
- `kernel/source/drivers/NVMe-Admin.c`
- `kernel/source/drivers/NVMe-Core.c`
- `kernel/source/drivers/NVMe-Disk.c`
- `kernel/source/drivers/NVMe-IO.c`

Principaux points:
- Mode polling-only.
- Timeout hybride (clock + loop fallback early boot).
- Synchronisation/mutex autour des submit/wait.
- Lecture CQ plus robuste (`volatile` + copie explicite).
- Détection taille logique de secteur par namespace (fin du hardcode 512 pour I/O).
- Réduction de flood warnings NVMe (cooldown).

## Tests exécutés
Commande relancée de nombreuses fois:
- `./scripts/build --arch x86-64 --fs ext2 --debug`

Résultat:
- Build OK après chaque série de modifications mentionnée.

Remarque:
- Aucun test bare metal automatisé possible ici; validation finale dépend des retours Predator.

## Ce que montrent les derniers logs Predator
- Les lignes de commande `dir` échouées sont bien en runtime (`Txxxxx` élevés), pas `T0`.
- On voit:
  - `[OpenFile] path=/fs/hd0p2 wildcard=1 ...`
  - puis `Mounted wildcard open failed local=*`
  - idem pour `/fs/hd0p2/Intel`.
- Conclusion intermédiaire:
  - la résolution SystemFS vers le volume monté fonctionne,
  - l'échec est dans l'ouverture wildcard déléguée au driver NTFS monté.

## Hypothèses traitées et statut
- "C'est seulement un problème de flood de logs": FAUX.
- "Le problème est uniquement boot/UEFI": FAUX pour l'échec `dir` runtime.
- "Le premier entry invalide casse toute l'énumération": probable, correctif appliqué, mais retour utilisateur indique pas d'amélioration visible.

## État actuel (important)
- Problème toujours reproduit sur Predator selon dernier retour: "aucune différence".
- Le diagnostic est meilleur (on sait où ça casse), mais le bug racine n'est pas encore neutralisé.

## Piste de correction suivante (non faite)
- Focaliser `NtfsOpenFile` wildcard path:
  - instrumenter et valider `TotalEntries`, `StoredEntries`, `MatchCount` (avant création handle),
  - vérifier contenu concret de `Entries[]` après filtrage pattern,
  - tracer valeur de retour de `NtfsLoadCurrentEnumerationEntry` avec index courant,
  - comparer comportement wildcard (`*`) vs open direct (`name` sans wildcard).
- Si nécessaire: fallback dans `NtfsOpenFile` pour listing folder non-wildcard via handle d'énumération robuste quand wildcard échoue sur volume partiellement corrompu.

## État git courant
Fichiers modifiés/non commit:
- `AGENTS.md`
- `documentation/Kernel.md`
- `kernel/include/Clock.h`
- `kernel/include/drivers/NVMe-Core.h`
- `kernel/include/drivers/NVMe-Internal.h`
- `kernel/include/utils/RateLimiter.h` (new)
- `kernel/source/Clock.c`
- `kernel/source/Log.c`
- `kernel/source/SystemFS.c`
- `kernel/source/drivers/NTFS-Base.c`
- `kernel/source/drivers/NTFS-Index.c`
- `kernel/source/drivers/NTFS-Path.c`
- `kernel/source/drivers/NTFS-Private.h`
- `kernel/source/drivers/NTFS-Record.c`
- `kernel/source/drivers/NTFS-VFS.c`
- `kernel/source/drivers/NVMe-Admin.c`
- `kernel/source/drivers/NVMe-Core.c`
- `kernel/source/drivers/NVMe-Disk.c`
- `kernel/source/drivers/NVMe-IO.c`
- `kernel/source/utils/RateLimiter.c` (new)

## Commit
- Aucun commit effectué dans cette étape (demande explicite utilisateur: ne pas commit sans ordre).
