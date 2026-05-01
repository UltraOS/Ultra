ultra_sources_if(
    CONFIG_PARAVIRT
    hypervisor.c
)
ultra_sources_if(
    CONFIG_KVM_GUEST
    kvm.c
)
