# Préparation des tâches et commutation de contexte : i386 vs x86-64

## Vue d'ensemble
Les architectures i386 et x86-64 d'EXOS reposent toutes deux sur `ArchSetupTask` pour initialiser le contexte d'exécution d'une tâche ainsi que sur `PrepareNextTaskSwitch` pour préparer la bascule orchestrée par le planificateur. Les macros d'assemblage situées dans `kernel/include/arch/...` finalisent la transmission des piles et des registres afin de réaliser l'`iret` ou l'`iretq` adéquat. Les sections suivantes détaillent chaque étape en mettant face à face les différences de conception entre le 32 bits et le mode long.

## Préparation d'une tâche (`ArchSetupTask`)

### Allocation et nettoyage des piles
- **i386** réserve une pile principale et une pile système par tâche, efface les deux zones et réinitialise le cadre d'interruption sauvegardé dans le contexte.【F:kernel/source/arch/i386/i386.c†L828-L874】
- **x86-64** suit la même séquence avec des bases et tailles 64 bits avant de nettoyer les piles et l'intégralité de la structure de contexte longue.【F:kernel/source/arch/x86-64/x86-64.c†L1063-L1111】

### Initialisation des registres et des sélecteurs
- **i386** positionne EAX/EBX pour fournir l'argument et l'adresse d'entrée de la tâche, charge les sélecteurs de segments selon le niveau de privilège, capture CR3/CR4 et pointe EIP vers le stub commun du lanceur de tâches.【F:kernel/source/arch/i386/i386.c†L875-L904】
- **x86-64** remplit les registres généraux 64 bits, mémorise CR4, fixe RIP/RFLAGS et conserve CR3 pour l'espace d'adressage long mode.【F:kernel/source/arch/x86-64/x86-64.c†L1113-L1126】

### Sélection de la pile selon le niveau de privilège
- **i386** choisit ESP/EBP soit sur la pile principale (ring 0), soit sur la pile système (ring 3) afin d'assurer la bonne pile lors des retours noyau ou utilisateur.【F:kernel/source/arch/i386/i386.c†L893-L904】
- **x86-64** sélectionne RSP/RBP sur la pile appropriée et range également SS0 et RSP0 dans le contexte sauvegardé pour réutilisation par la TSS longue lors d'un retour noyau.【F:kernel/source/arch/x86-64/x86-64.c†L1128-L1140】

### Bootstrap de la tâche noyau principale
- **i386** marque la tâche principale comme active, programme ESP0 dans la TSS matérielle, mesure la consommation de la pile de démarrage puis bascule l'exécution sur la nouvelle pile via `SwitchStack` avant de mettre à jour EBP.【F:kernel/source/arch/i386/i386.c†L906-L929】
- **x86-64** réalise la même migration en limitant le nombre d'octets copiés à 32 bits, remet RSP à zéro pour la tâche principale et conserve RBP ainsi que RSP0 dans la structure de contexte pour les transitions futures.【F:kernel/source/arch/x86-64/x86-64.c†L1142-L1178】

## Cadres d'interruption et structure TSS

### TSS et cadre d'interruption i386
La TSS i386 conserve l'image complète des registres généraux, des sélecteurs de segments et la carte d'E/S sur 1024 ports. Le cadre d'interruption associé ajoute SS0/ESP0 afin que les entrées d'IDT puissent replacer le processeur sur la pile noyau partagée lors d'une transition depuis le ring 3.【F:kernel/include/arch/i386/i386.h†L361-L423】

### TSS longue x86-64 et Interrupt Stack Table
La TSS longue se limite à stocker les pointeurs de piles RSP0–RSP2, sept entrées IST (IST1–IST7) et un pointeur `IOMapBase`. Le cadre d'interruption 64 bits reflète ces champs en doublant SS0/RSP0 pour garder la cohérence lors d'un `iretq`. EXOS réplique systématiquement le RSP0 de la tâche suivante à la fois dans `RSP0` et dans `IST1` afin que les exceptions configurées sur IST1 (par exemple double faute) bénéficient d'une pile noyau propre, distincte de celle potentiellement corrompue de la tâche interrompue.【F:kernel/include/arch/x86-64/x86-64.h†L258-L287】【F:kernel/source/arch/x86-64/x86-64.c†L1187-L1208】 Cette duplication est inutile en i386 où le couple SS0/ESP0 suffit, faute de mécanisme IST.【F:kernel/source/arch/i386/i386.c†L945-L963】

## Préparation d'un changement de tâche (`PrepareNextTaskSwitch`)

### Programmation de la pile noyau
- **i386** recalcule le sommet de la pile système de la tâche suivante, met à jour SS0/ESP0 dans la TSS partagée et garantit ainsi que toute transition ring 3 → ring 0 retombera sur la pile adéquate.【F:kernel/source/arch/i386/i386.c†L945-L950】
- **x86-64** récupère la TSS longue, copie RSP0 dans le champ principal et dans IST1, puis positionne `IOMapBase` à la taille de la structure pour désactiver le filtrage de ports par la carte d'E/S.【F:kernel/source/arch/x86-64/x86-64.c†L1187-L1208】

### Sauvegarde et restauration d'état
- **i386** sauvegarde FS/GS et l'état FPU de la tâche sortante, recharge le répertoire de pages de la tâche entrante, restaure ses segments et son contexte FPU avant de redonner la main au trampoline assembleur.【F:kernel/source/arch/i386/i386.c†L951-L963】
- **x86-64** conditionne la sauvegarde FS/GS/FPU à la présence d'une tâche courante, applique les mêmes opérations de pagination et de segments, puis restaure l'image FPU de la tâche cible.【F:kernel/source/arch/x86-64/x86-64.c†L1196-L1221】

## Macros de transfert de contexte

### Construction du cadre de retour
- **i386** empile les paires CS/EIP puis, pour un retour utilisateur, ajoute SS/ESP avant l'`iret`, ce qui prépare un cadre 32 bits minimaliste.【F:kernel/include/arch/i386/i386.h†L508-L520】
- **x86-64** réserve explicitement des emplacements 64 bits pour RIP, CS, RFLAGS et, en mode utilisateur, pour SS/RSP, répondant aux exigences du `iretq`.【F:kernel/include/arch/x86-64/x86-64.h†L333-L348】

### Trampoline assembleur de bascule
- **i386** s'appuie sur l'instruction `pusha` pour capturer l'intégralité des registres généraux, change de pile, appelle `SwitchToNextTask_3` puis restaure l'état via `popa`, profitant de l'instruction compacte disponible sur 32 bits.【F:kernel/include/arch/i386/i386.h†L522-L539】
- **x86-64** pousse manuellement RAX à R15, enregistre RSP/RIP, échange la pile avec celle de la tâche suivante, invoque `SwitchToNextTask_3` et dépile chaque registre car aucun équivalent 64 bits de `pusha` n'existe en mode long.【F:kernel/include/arch/x86-64/x86-64.h†L351-L401】

### Relance de la tâche prête
- **i386** recharge EAX/EBX depuis le contexte sauvegardé, positionne ESP sur la pile préparée et exécute `iret` pour rejoindre le stub d'exécution.【F:kernel/include/arch/i386/i386.h†L541-L550】
- **x86-64** reproduit cette séquence avec les registres 64 bits et l'instruction `iretq` nécessaire au mode long.【F:kernel/include/arch/x86-64/x86-64.h†L403-L414】

## Différences clés à retenir
- La TSS longue remplace l'image complète des registres par un simple répartiteur de piles (RSP0–RSP2 + IST1–IST7), d'où la duplication de RSP0 dans IST1 sur x86-64 alors que i386 se contente de SS0/ESP0 pour toutes les transitions.【F:kernel/include/arch/x86-64/x86-64.h†L258-L287】【F:kernel/source/arch/x86-64/x86-64.c†L1187-L1208】【F:kernel/source/arch/i386/i386.c†L945-L963】
- Le trampoline x86-64 doit gérer manuellement les registres supplémentaires r8–r15 et utiliser des emplacements 64 bits pour les cadres de retour, tandis que la version i386 exploite `pusha`/`iret` pour encapsuler la même logique en 32 bits.【F:kernel/include/arch/x86-64/x86-64.h†L333-L414】【F:kernel/include/arch/i386/i386.h†L508-L550】
