namespace kickcat
{
    template<typename T>
    std::tuple<DatagramHeader const*, T const*, uint16_t> Bus::nextDatagram()
    {
        auto* frame = &frames_[current_frame_];
        if (not frame->isDatagramAvailable())
        {
            // no more data to read
            current_frame_ = 0;
            return std::make_tuple(nullptr, nullptr, 0);
        }

        auto [header, payload, wkc] = frame->nextDatagram();
        if (not frame->isDatagramAvailable())
        {
            ++current_frame_;
            if (current_frame_ < frames_.size()) // Is the next frame have available datagram?
            {
                frame = &frames_[current_frame_];
                if (not frame->isDatagramAvailable())
                {
                    current_frame_ = 0;
                }
            }
            else
            {
                current_frame_ = 0; // last frame reached, rollback
            }
        }
        void const* p2 = payload;
        T const* data = static_cast<T const*>(p2);
        return std::make_tuple(header, data, wkc);
    }
}
