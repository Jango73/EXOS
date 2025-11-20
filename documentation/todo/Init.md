
L'objectif de cette tâche est de transformer les appels contenus dans InitializeKernel en une liste de drivers.

Ci-dessous la séquence actuelle d'initialisation du noyau (InitializeKernel()).

```
    PreInitializeKernel()
    InitializeConsole()
    InitKernelLog()
    InitializeMemoryManager()
    InitializeTaskSegments()
    CheckDataIntegrity()
    InitializeInterrupts()
    DumpCriticalInformation()
    InitializeKernelProcess()
    InitializeACPI()
    InitializeLocalAPIC()
    InitializeIOAPIC()
    InitializeInterruptController()
    CacheInit(&Kernel.ObjectTerminationCache, CACHE_DEFAULT_CAPACITY)
    HandleMapInit(&Kernel.HandleMap)
    RunAllTests()
    LoadDriver(&StdKeyboardDriver, TEXT("Keyboard"))
    LoadDriver(&SerialMouseDriver, TEXT("SerialMouse"))
    InitializeClock()
    GetCPUInformation(&(Kernel.CPU))
    InitializeQuantumTime()
    InitializePCI()
    LoadDriver(&ATADiskDriver, TEXT("ATADisk"))
    LoadDriver(&SATADiskDriver, TEXT("SATADisk"))
    LoadDriver(&RAMDiskDriver, TEXT("RAMDisk"))
    InitializeFileSystems()
    ReadKernelConfiguration()
    InitializeDeviceInterrupts()
    InitializeDeferredWork()
    InitializeNetwork()
    MountUserNodes()
    InitializeUserSystem()
    LoadDriver(&VESADriver, TEXT("VESA"))
```

Tâche à accomplir :

- Chacune des fonctions listées doit être maintenant vue comme le chargement d'un driver (certaines le sont déjà : LoadDriver(...))
- Pour chaque module d'où proviennent les fonctions listées :
  - Instancier une structure de type DRIVER déclarée dans le .h du module et vivant dans le .c du module.
  - Implémenter :
    - DF_LOAD : appelle la fonction d'init du module (exemple : InitializeMemoryManager)
    - DF_UNLOAD : appelle la fonction de deinit du module (si elle existe)
- Créer une liste dans KernelData.c:Kernel qui va contenir tous les DRIVER définis dans ces modules, déclarés dans le MEME ORDRE que les modules apparaissent dans InitializeKernel(). Cette liste s'appelle Drivers.
- InitializeKernel() va donc se contenter de parcourir la liste de Drivers, et pour chaque driver :
  - appeler LoadDriver(driver, driver->Product)
  - si LoadDriver retourne vrai, affiche un DEBUG() d'info
  - si LoadDriver retourne faux:
    - si le driver est critique : paniquer
    - si le driver n'est pas critique, ajouter un ERROR() d'info
- Concernant les DRIVER :
  - Ajouter ces champs dans DRIVER_FIELDS :
    - Ready : faux par défaut, vrai quand le driver a été chargé (son DF_LOAD a été appelé)
    - Critical : sa valeur dépend de sa criticité système (exemple : PreInitializeKernel, InitializeMemoryManager, InitializeTaskSegments, ...)
  - si la fonction DF_LOAD est appelée alors que Driver.Ready est vrai: ne rien faire
  - si la fonction DF_UNLOAD est appelée alors que Driver.Ready est faux: ne rien faire
  - si le module contient déjà une fonction de shutdown, alors DF_UNLOAD doit l'appeler. sinon, DF_UNLOAD ne fait rien (à part mettre Ready à faux).

Avant de coder, tu vas créer un plan pour conduire cette tâche, dans un nouveau .md, et tu attends que je valide.
