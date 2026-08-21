#pragma once

// Remap both PICs out of the exception range and mask every line
void i8259a_quiesce(void);
