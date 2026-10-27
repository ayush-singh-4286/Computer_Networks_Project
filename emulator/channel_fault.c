
#include <stdlib.h>
#include "channel_emulator.h"

static double random_probability(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

void initialize_channel(ChannelConfig *config)
{
    if (config == NULL)
        return;

    srand(config->seed);
}

int should_drop_packet(const ChannelConfig *config)
{
    return config != NULL &&
           random_probability() < config->loss_rate;
}

int should_duplicate_packet(const ChannelConfig *config)
{
    return config != NULL &&
           random_probability() < config->duplicate_rate;
}

int should_corrupt_packet(const ChannelConfig *config)
{
    return config != NULL &&
           random_probability() < config->corruption_rate;
}

int should_reorder_packet(const ChannelConfig *config)
{
    return config != NULL &&
           random_probability() < config->reorder_rate;
}

