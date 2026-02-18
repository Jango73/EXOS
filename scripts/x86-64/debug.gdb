define setup
  display/x $cs
  display/x $ss
  display/s CurrentTask->Name
  display/s NextTask->Name
  display/x CurrentTask->Arch.Context.Registers.RAX
  display/x CurrentTask->Arch.Context.Registers.RBX
  display/x NextTask->Arch.Context.Registers.RAX
  display/x NextTask->Arch.Context.Registers.RBX
  display/x Kernel_x86_32.TSS->RSP0
  display/x Kernel_x86_32.TSS->IST1
  display
end
