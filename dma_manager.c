/**
 * @file dma_manager.c
 * @brief Implementation of centralized DMA channel management system
 */

#include "dma_manager.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

// Static array to track DMA channel status
static dma_channel_info_t dma_channels[DMA_NUM_CHANNELS];

// Mutex for thread-safe access to the DMA channel status
static mutex_t dma_mutex;

// Initialize the DMA manager
void dma_manager_init(void) {
    // Initialize the mutex
    mutex_init(&dma_mutex);
    
    // Initialize all channels as free
    for (int i = 0; i < DMA_NUM_CHANNELS; i++) {
        dma_channels[i].status = DMA_CHANNEL_STATUS_FREE;
        dma_channels[i].owner = NULL;
        dma_channels[i].core_num = 0xFF; // Invalid core number
        dma_channels[i].irq_handler = NULL; // No IRQ handler initially
    }
    
    printf("DMA Manager: Initialized with %d channels\n", DMA_NUM_CHANNELS);
    printf("DMA Manager: Core 0 channels: %d-%d\n", DMA_CORE0_CHANNEL_START, DMA_CORE0_CHANNEL_END);
    printf("DMA Manager: Core 1 channels: %d-%d\n", DMA_CORE1_CHANNEL_START, DMA_CORE1_CHANNEL_END);
}

// Request a specific DMA channel
bool dma_manager_request_channel(uint channel, const char* owner) {
    bool success = false;
    
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        printf("DMA Manager: Invalid channel number %d\n", channel);
        return false;
    }
    
    // Get current core number
    uint core_num = get_core_num();
    
    // Check if the channel is in the appropriate range for this core
    bool is_valid_for_core = false;
    if (core_num == 0 && channel >= DMA_CORE0_CHANNEL_START && channel <= DMA_CORE0_CHANNEL_END) {
        is_valid_for_core = true;
    } else if (core_num == 1 && channel >= DMA_CORE1_CHANNEL_START && channel <= DMA_CORE1_CHANNEL_END) {
        is_valid_for_core = true;
    }
    
    if (!is_valid_for_core) {
        printf("DMA Manager: Channel %d is not valid for core %d\n", channel, core_num);
        // We'll still allow it, but warn about it
        printf("DMA Manager: WARNING - Using channel outside of core's reserved range\n");
    }
    
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    // Check if the channel is available
    if (dma_channels[channel].status == DMA_CHANNEL_STATUS_FREE) {
        // Reserve the channel
        dma_channels[channel].status = DMA_CHANNEL_STATUS_RESERVED;
        dma_channels[channel].owner = owner;
        dma_channels[channel].core_num = core_num;
        
        // Claim the channel in hardware
        dma_claim_mask(1u << channel);
        
        success = true;
        printf("DMA Manager: Channel %d reserved by '%s' on core %d\n", channel, owner, core_num);
    } else {
        printf("DMA Manager: Channel %d already in use by '%s'\n", 
               channel, dma_channels[channel].owner);
    }
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return success;
}

// Release a previously requested DMA channel
bool dma_manager_release_channel(uint channel) {
    bool success = false;
    
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        printf("DMA Manager: Invalid channel number %d\n", channel);
        return false;
    }
    
    // Get current core number
    uint core_num = get_core_num();
    
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    // Check if the channel is in use
    if (dma_channels[channel].status != DMA_CHANNEL_STATUS_FREE) {
        // Check if the channel is being released by the same core that reserved it
        if (dma_channels[channel].core_num != core_num) {
            printf("DMA Manager: WARNING - Channel %d being released by core %d but was reserved by core %d\n",
                   channel, core_num, dma_channels[channel].core_num);
        }
        
        // Release the channel
        dma_channels[channel].status = DMA_CHANNEL_STATUS_FREE;
        printf("DMA Manager: Channel %d released (was owned by '%s')\n", 
               channel, dma_channels[channel].owner);
        dma_channels[channel].owner = NULL;
        dma_channels[channel].core_num = 0xFF; // Invalid core number
        
        // Release the channel in hardware
        dma_channel_unclaim(channel);
        
        success = true;
    } else {
        printf("DMA Manager: Channel %d is not in use\n", channel);
    }
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return success;
}

// Check if a DMA channel is available
bool dma_manager_is_channel_available(uint channel) {
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        return false;
    }
    
    bool available = false;
    
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    // Check if the channel is free
    available = (dma_channels[channel].status == DMA_CHANNEL_STATUS_FREE);
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return available;
}

// Get the owner of a DMA channel
const char* dma_manager_get_channel_owner(uint channel) {
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        return NULL;
    }
    
    const char* owner = NULL;
    
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    // Get the owner
    owner = dma_channels[channel].owner;
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return owner;
}

// Validate all DMA channel assignments
bool dma_manager_validate_channels(void) {
    bool valid = true;
    
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    // Check for any conflicts
    printf("DMA Manager: Validating channel assignments...\n");
    
    for (int i = 0; i < DMA_NUM_CHANNELS; i++) {
        if (dma_channels[i].status != DMA_CHANNEL_STATUS_FREE) {
            // Check if this channel is in the correct range for its core
            uint core_num = dma_channels[i].core_num;
            bool is_valid_for_core = false;
            
            if (core_num == 0 && i >= DMA_CORE0_CHANNEL_START && i <= DMA_CORE0_CHANNEL_END) {
                is_valid_for_core = true;
            } else if (core_num == 1 && i >= DMA_CORE1_CHANNEL_START && i <= DMA_CORE1_CHANNEL_END) {
                is_valid_for_core = true;
            }
            
            if (!is_valid_for_core) {
                printf("DMA Manager: WARNING - Channel %d is used by core %d but is outside its reserved range\n",
                       i, core_num);
                // Don't fail validation for this, just warn
            }
            
            printf("DMA Manager: Channel %d is used by '%s' on core %d\n", 
                   i, dma_channels[i].owner, dma_channels[i].core_num);
        }
    }
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return valid;
}

// Print the status of all DMA channels
void dma_manager_print_status(void) {
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    printf("DMA Manager: Channel Status\n");
    printf("-------------------------\n");
    
    for (int i = 0; i < DMA_NUM_CHANNELS; i++) {
        printf("Channel %2d: ", i);
        
        switch (dma_channels[i].status) {
            case DMA_CHANNEL_STATUS_FREE:
                printf("FREE\n");
                break;
            case DMA_CHANNEL_STATUS_RESERVED:
                printf("RESERVED by '%s' on core %d\n", 
                       dma_channels[i].owner, dma_channels[i].core_num);
                break;
            case DMA_CHANNEL_STATUS_IN_USE:
                printf("IN USE by '%s' on core %d\n", 
                       dma_channels[i].owner, dma_channels[i].core_num);
                break;
            default:
                printf("UNKNOWN STATUS\n");
                break;
        }
    }
    
    printf("-------------------------\n");
    
    // Release mutex
    mutex_exit(&dma_mutex);
}

// Get a channel reserved for the current core
int dma_manager_get_core_channel(const char* owner) {
    int assigned_channel = -1;
    uint core_num = get_core_num();
    
    // Determine the range of channels for this core
    uint start_channel = (core_num == 0) ? DMA_CORE0_CHANNEL_START : DMA_CORE1_CHANNEL_START;
    uint end_channel = (core_num == 0) ? DMA_CORE0_CHANNEL_END : DMA_CORE1_CHANNEL_END;
    
    // Acquire mutex for thread-safe access
    mutex_enter_blocking(&dma_mutex);
    
    // Look for a free channel in the core's range
    for (uint i = start_channel; i <= end_channel; i++) {
        if (dma_channels[i].status == DMA_CHANNEL_STATUS_FREE) {
            // Found a free channel, reserve it
            dma_channels[i].status = DMA_CHANNEL_STATUS_RESERVED;
            dma_channels[i].owner = owner;
            dma_channels[i].core_num = core_num;
            
            // Claim the channel in hardware
            dma_claim_mask(1u << i);
            
            assigned_channel = i;
            printf("DMA Manager: Assigned channel %d to '%s' on core %d\n", 
                   i, owner, core_num);
            break;
        }
    }
    
    if (assigned_channel == -1) {
        printf("DMA Manager: No free channels available for core %d\n", core_num);
    }
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return assigned_channel;
}

// Static flags to track interrupt initialization
static bool dma_irq0_initialized = false;
static bool dma_irq1_initialized = false;

/**
 * @brief Unified DMA IRQ 0 handler
 * 
 * This handler checks which DMA channels have triggered interrupts
 * and dispatches to the appropriate registered handlers.
 */
static void dma_unified_irq0_handler(void) {
    // Check all channels that use DMA_IRQ_0
    for (uint channel = 0; channel < DMA_NUM_CHANNELS; channel++) {
        if (dma_channel_get_irq0_status(channel)) {
            // Clear the interrupt
            dma_channel_acknowledge_irq0(channel);
            
            // Call the registered handler if one exists
            if (dma_channels[channel].irq_handler != NULL) {
                dma_channels[channel].irq_handler(channel);
            }
        }
    }
}

/**
 * @brief Unified DMA IRQ 1 handler
 * 
 * This handler checks which DMA channels have triggered interrupts
 * and dispatches to the appropriate registered handlers.
 */
static void dma_unified_irq1_handler(void) {
    // Check all channels that use DMA_IRQ_1
    for (uint channel = 0; channel < DMA_NUM_CHANNELS; channel++) {
        if (dma_channel_get_irq1_status(channel)) {
            // Clear the interrupt
            dma_channel_acknowledge_irq1(channel);
            
            // Call the registered handler if one exists
            if (dma_channels[channel].irq_handler != NULL) {
                dma_channels[channel].irq_handler(channel);
            }
        }
    }
}

// Initialize DMA interrupt system
void dma_manager_init_interrupts(void) {
    if (!dma_irq0_initialized) {
        irq_set_exclusive_handler(DMA_IRQ_0, dma_unified_irq0_handler);
        irq_set_enabled(DMA_IRQ_0, true);
        dma_irq0_initialized = true;
        printf("DMA Manager: Unified DMA_IRQ_0 handler installed\n");
    }
    
    if (!dma_irq1_initialized) {
        irq_set_exclusive_handler(DMA_IRQ_1, dma_unified_irq1_handler);
        irq_set_enabled(DMA_IRQ_1, true);
        dma_irq1_initialized = true;
        printf("DMA Manager: Unified DMA_IRQ_1 handler installed\n");
    }
}

// Register an IRQ handler for a specific DMA channel
bool dma_manager_register_irq_handler(uint channel, dma_irq_handler_t handler) {
    bool success = false;
    
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        printf("DMA Manager: Invalid channel number %d for IRQ handler registration\n", channel);
        return false;
    }
    
    if (handler == NULL) {
        printf("DMA Manager: NULL handler provided for channel %d\n", channel);
        return false;
    }
    
    // Lock mutex
    mutex_enter_blocking(&dma_mutex);
    
    // Check if channel is owned by someone
    if (dma_channels[channel].status == DMA_CHANNEL_STATUS_IN_USE) {
        dma_channels[channel].irq_handler = handler;
        success = true;
        printf("DMA Manager: IRQ handler registered for channel %d (owner: %s)\n", 
               channel, dma_channels[channel].owner);
    } else {
        printf("DMA Manager: Cannot register IRQ handler for unowned channel %d\n", channel);
    }
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return success;
}

// Unregister an IRQ handler for a specific DMA channel
bool dma_manager_unregister_irq_handler(uint channel) {
    bool success = false;
    
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        printf("DMA Manager: Invalid channel number %d for IRQ handler unregistration\n", channel);
        return false;
    }
    
    // Lock mutex
    mutex_enter_blocking(&dma_mutex);
    
    if (dma_channels[channel].irq_handler != NULL) {
        dma_channels[channel].irq_handler = NULL;
        success = true;
        printf("DMA Manager: IRQ handler unregistered for channel %d\n", channel);
    }
    
    // Release mutex
    mutex_exit(&dma_mutex);
    
    return success;
}

// Enable DMA interrupts for a specific channel
bool dma_manager_enable_channel_irq(uint channel, uint irq_num) {
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        printf("DMA Manager: Invalid channel number %d for IRQ enable\n", channel);
        return false;
    }
    
    // Validate IRQ number
    if (irq_num > 1) {
        printf("DMA Manager: Invalid IRQ number %d (must be 0 or 1)\n", irq_num);
        return false;
    }
    
    // Check if channel is owned
    if (dma_channels[channel].status != DMA_CHANNEL_STATUS_IN_USE) {
        printf("DMA Manager: Cannot enable IRQ for unowned channel %d\n", channel);
        return false;
    }
    
    // Enable the appropriate interrupt
    if (irq_num == 0) {
        dma_channel_set_irq0_enabled(channel, true);
        printf("DMA Manager: Enabled DMA_IRQ_0 for channel %d\n", channel);
    } else {
        dma_channel_set_irq1_enabled(channel, true);
        printf("DMA Manager: Enabled DMA_IRQ_1 for channel %d\n", channel);
    }
    
    return true;
}

// Disable DMA interrupts for a specific channel
bool dma_manager_disable_channel_irq(uint channel, uint irq_num) {
    // Validate channel number
    if (channel >= DMA_NUM_CHANNELS) {
        printf("DMA Manager: Invalid channel number %d for IRQ disable\n", channel);
        return false;
    }
    
    // Validate IRQ number
    if (irq_num > 1) {
        printf("DMA Manager: Invalid IRQ number %d (must be 0 or 1)\n", irq_num);
        return false;
    }
    
    // Disable the appropriate interrupt
    if (irq_num == 0) {
        dma_channel_set_irq0_enabled(channel, false);
        printf("DMA Manager: Disabled DMA_IRQ_0 for channel %d\n", channel);
    } else {
        dma_channel_set_irq1_enabled(channel, false);
        printf("DMA Manager: Disabled DMA_IRQ_1 for channel %d\n", channel);
    }
    
    return true;
}