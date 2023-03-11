/*
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/export.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#ifdef CONFIG_AMLOGIC_DEBUG_FTRACE_PSTORE
#include <linux/moduleparam.h>
#include <linux/amlogic/debug_ftrace_ramoops.h>
#endif

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
static int __read_mostly ioremap_pud_capable;
static int __read_mostly ioremap_pmd_capable;
static int __read_mostly ioremap_huge_disabled;

static int __init set_nohugeiomap(char *str)
{
	ioremap_huge_disabled = 1;
	return 0;
}
early_param("nohugeiomap", set_nohugeiomap);

void __init ioremap_huge_init(void)
{
	if (!ioremap_huge_disabled) {
		if (arch_ioremap_pud_supported())
			ioremap_pud_capable = 1;
		if (arch_ioremap_pmd_supported())
			ioremap_pmd_capable = 1;
	}
}

static inline int ioremap_pud_enabled(void)
{
	return ioremap_pud_capable;
}

static inline int ioremap_pmd_enabled(void)
{
	return ioremap_pmd_capable;
}

#else	/* !CONFIG_HAVE_ARCH_HUGE_VMAP */
static inline int ioremap_pud_enabled(void) { return 0; }
static inline int ioremap_pmd_enabled(void) { return 0; }
#endif	/* CONFIG_HAVE_ARCH_HUGE_VMAP */

static int ioremap_pte_range(pmd_t *pmd, unsigned long addr,
		unsigned long end, phys_addr_t phys_addr, pgprot_t prot)
{
	pte_t *pte;
	u64 pfn;

	pfn = phys_addr >> PAGE_SHIFT;
	pte = pte_alloc_kernel(pmd, addr);
	if (!pte)
		return -ENOMEM;
	do {
		BUG_ON(!pte_none(*pte));
		set_pte_at(&init_mm, addr, pte, pfn_pte(pfn, prot));
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	return 0;
}

static inline int ioremap_pmd_range(pud_t *pud, unsigned long addr,
		unsigned long end, phys_addr_t phys_addr, pgprot_t prot)
{
	pmd_t *pmd;
	unsigned long next;

	phys_addr -= addr;
	pmd = pmd_alloc(&init_mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);

		if (ioremap_pmd_enabled() &&
		    ((next - addr) == PMD_SIZE) &&
		    IS_ALIGNED(phys_addr + addr, PMD_SIZE) &&
		    pmd_free_pte_page(pmd)) {
			if (pmd_set_huge(pmd, phys_addr + addr, prot))
				continue;
		}

		if (ioremap_pte_range(pmd, addr, next, phys_addr + addr, prot))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int ioremap_pud_range(pgd_t *pgd, unsigned long addr,
		unsigned long end, phys_addr_t phys_addr, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;

	phys_addr -= addr;
	pud = pud_alloc(&init_mm, pgd, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);

		if (ioremap_pud_enabled() &&
		    ((next - addr) == PUD_SIZE) &&
		    IS_ALIGNED(phys_addr + addr, PUD_SIZE) &&
		    pud_free_pmd_page(pud)) {
			if (pud_set_huge(pud, phys_addr + addr, prot))
				continue;
		}

		if (ioremap_pmd_range(pud, addr, next, phys_addr + addr, prot))
			return -ENOMEM;
	} while (pud++, addr = next, addr != end);
	return 0;
}

#ifdef CONFIG_AMLOGIC_DEBUG_FTRACE_PSTORE
bool is_normal_memory(pgprot_t p)
{
#if defined(CONFIG_ARM)
	return ((pgprot_val(p) & L_PTE_MT_MASK) == L_PTE_MT_WRITEALLOC);
#elif defined(CONFIG_ARM64)
	return (pgprot_val(p) & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL);
#else
#error "Unuspported architecture"
#endif
}
#endif

int ioremap_page_range(unsigned long addr,
		       unsigned long end, phys_addr_t phys_addr, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long start;
	unsigned long next;
	int err;

	BUG_ON(addr >= end);

	start = addr;
	phys_addr -= addr;
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		err = ioremap_pud_range(pgd, addr, next, phys_addr+addr, prot);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	flush_cache_vmap(start, end);
#ifdef CONFIG_AMLOGIC_DEBUG_FTRACE_PSTORE
	if (need_dump_iomap() && !is_normal_memory(prot))
		pr_err("io__map <va:0x%08lx-0x%08lx> pa:0x%lx,port:0x%lx\n",
		       start, end, (unsigned long)phys_addr,
		       (unsigned long)pgprot_val(prot));
#endif
	return err;
}
EXPORT_SYMBOL_GPL(ioremap_page_range);
