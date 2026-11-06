import { create } from 'zustand';

interface DialogState {
  open: boolean;
  setOpen: (next: boolean) => void;
}

export const useDialogStore = create<DialogState>((set) => ({
  open: false,
  setOpen: (next) => set({ open: next }),
}));
