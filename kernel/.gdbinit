# Wait until paging is enabled (PG bit in CR0 flips to 1)
define wait_pg
  while (($cr0 & 0x80000000) == 0)
    si
  end
  echo PG enabled at EIP=
  p/x $eip
  x/8i $eip
end

# Quick check that the kernel copy at 0x20000 looks sane
define chk_copy
  echo Dump @0x20000:\n
  x/32wx 0x20000
end
